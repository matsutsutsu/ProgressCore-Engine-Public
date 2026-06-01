#include "ParticleRenderer.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/RenderState.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Core/Math/StringHash.h" // "_hash" リテラル用
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

void ParticleRenderer::Initialize(ID3D11Device *device)
{
    // 2. サンプラー作成
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU           = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV           = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW           = D3D11_TEXTURE_ADDRESS_WRAP;
    device->CreateSamplerState(&sampDesc, sampler.GetAddressOf());

    // 3. デフォルトテクスチャ作成 (白1x1ピクセル)
    uint32_t             white      = 0xFFFFFFFF;
    D3D11_TEXTURE2D_DESC texDesc    = {};
    texDesc.Width                   = 1;
    texDesc.Height                  = 1;
    texDesc.MipLevels               = 1;
    texDesc.ArraySize               = 1;
    texDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count        = 1;
    texDesc.Usage                   = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags               = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initData = {&white, sizeof(uint32_t), 0};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    device->CreateTexture2D(&texDesc, &initData, tex.GetAddressOf());
    device->CreateShaderResourceView(tex.Get(), nullptr, defaultTexture.GetAddressOf());


    // ★ 金型の事前構築
    // 半透明ベースをもらってきて、一部（トポロジーとブレンド）だけをパーティクル用に改造する！
    PipelineStateDesc baseDesc = PipelineStateDesc::DefaultTransparent();
    baseDesc.topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST; // 点に変更

    // ① 加算用
    baseDesc.blend = BlendState::Additive;
    _psoAdditive = PipelineState(baseDesc);

    // ② 半透明用
    baseDesc.blend = BlendState::Transparency;
    _psoTransparent = PipelineState(baseDesc);


    _isLoaded = true;
}

void ParticleRenderer::BeginFrame()
{
    _drawCommands.clear(); // 毎フレームバケツを空にする
}

void ParticleRenderer::Draw(const ParticleDrawCommand &cmd)
{
    _drawCommands.push_back(cmd); // バケツに積むだけ
}

void ParticleRenderer::Render(const RenderServices& services, const RenderFrameContext& frameCtx)
{
    if (!_isLoaded || _drawCommands.empty()) return;

    ID3D11DeviceContext* dc = frameCtx.dc;
    const RenderState* rs = services.renderState;

    // =========================================================
    // ★ 修正: Registryからシェーダーを取得し、Begin()を委譲
    // =========================================================
    Shader* shader = services.shaders->GetShader("Particle"_hash, services.device);
    if (!shader) return;

    shader->Begin(dc, services, frameCtx);

    // 入力アセンブリ設定
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

    // サンプラー設定
    ID3D11SamplerState* samplers[] = { sampler.Get() };
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, samplers);

    // シーン定数バッファ (カメラ情報) を共通でバインド
    if (services.sceneConstantBuffer) {
        ID3D11Buffer* cbs[] = { services.sceneConstantBuffer };
        dc->VSSetConstantBuffers(SLOT_CB_SCENE, 1, cbs);
        dc->GSSetConstantBuffers(SLOT_CB_SCENE, 1, cbs);
        dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, cbs);
    }

    // =========================================================================
    // ★ 修正: ソート処理のラムダ内で、シングルトンではなく services を参照する
    // =========================================================================
    std::sort(_drawCommands.begin(), _drawCommands.end(), [&services](const ParticleDrawCommand& a, const ParticleDrawCommand& b) {
        if (a.render_mode != b.render_mode) {
            return a.render_mode < b.render_mode;
        }
        ID3D11ShaderResourceView* texA = services.resources->GetTexture(a.texture);
        ID3D11ShaderResourceView* texB = services.resources->GetTexture(b.texture);
        return texA < texB;
        });

    int currentRenderMode = -1;
    ID3D11ShaderResourceView* currentTextureSRV = reinterpret_cast<ID3D11ShaderResourceView*>(-1);

    // 溜まったコマンドを一気に描画
    for (const auto& cmd : _drawCommands) {
        // ★ 修正: ResourceManager::Instance() を排除
        GpuParticleBufferSet* buffers = services.resources->GetParticleBuffer(cmd.computeBuffer);
        if (!buffers || !buffers->particleSRV) continue;

        if (currentRenderMode != cmd.render_mode) {
            if (cmd.render_mode == 0) _psoAdditive.Apply(dc, rs);
            else                      _psoTransparent.Apply(dc, rs);

            dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestOnly), 0);
            currentRenderMode = cmd.render_mode;
        }

        // バッファバインド
        ID3D11ShaderResourceView* srvs[] = { buffers->particleSRV };
        dc->VSSetShaderResources(SLOT_SRV_PARTICLE_BUF, 1, srvs);
        dc->GSSetShaderResources(SLOT_SRV_PARTICLE_BUF, 1, srvs);

        // パーティクル定数バッファバインド
        if (buffers->constantBuffer) {
            ID3D11Buffer* particleCBs[] = { buffers->constantBuffer };
            dc->VSSetConstantBuffers(SLOT_CB_PARTICLE, 1, particleCBs);
            dc->GSSetConstantBuffers(SLOT_CB_PARTICLE, 1, particleCBs);
            dc->PSSetConstantBuffers(SLOT_CB_PARTICLE, 1, particleCBs);
        }

        // テクスチャバインド
        ID3D11ShaderResourceView* mainTexSRV = services.resources->GetTexture(cmd.texture);
        ID3D11ShaderResourceView* actualTexSRV = mainTexSRV ? mainTexSRV : defaultTexture.Get();

        if (currentTextureSRV != actualTexSRV) {
            ID3D11ShaderResourceView* texs[] = { actualTexSRV };
            dc->PSSetShaderResources(SLOT_SRV_MAT_TEX0, 1, texs);
            currentTextureSRV = actualTexSRV;
        }

        dc->Draw(cmd.max_particle_count, 0);
    }

    // =========================================================
    // ★ 修正: シェーダーの後始末を End() に委譲
    // =========================================================
    shader->End(dc, services);

    // 固有のクリーンアップ（GSSetShaderの解除等はEnd()で行われます）
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    dc->VSSetShaderResources(SLOT_SRV_PARTICLE_BUF, 1, nullSRVs);
    dc->GSSetShaderResources(SLOT_SRV_PARTICLE_BUF, 1, nullSRVs);
    dc->PSSetShaderResources(SLOT_SRV_MAT_TEX0, 1, nullSRVs);

    dc->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(nullptr, 0);
}