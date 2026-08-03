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
    SetDebugName(defaultTexture.Get(), "Particle_DefaultTexture_SRV");

    // ★ 金型の事前構築
    // 半透明ベースをもらってきて、一部（トポロジーとブレンド）だけをパーティクル用に改造する！
    PipelineStateDesc baseDesc = PipelineStateDesc::DefaultTransparent();
    baseDesc.topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST; // 点に変更

    // ① 加算用
    baseDesc.blend = BlendState::Additive;
    m_psoAdditive = PipelineState(baseDesc);

    // ② 半透明用
    baseDesc.blend = BlendState::Transparency;
    m_psoTransparent = PipelineState(baseDesc);


    m_isLoaded = true;
}

void ParticleRenderer::BeginFrame()
{
    m_drawCommands.clear(); // 毎フレームバケツを空にする
}

void ParticleRenderer::Draw(const ParticleDrawCommand &cmd)
{
    m_drawCommands.push_back(cmd); // バケツに積むだけ
}

void ParticleRenderer::Render(const RenderServices& services, const RenderFrameContext& frameCtx)
{
    if (!m_isLoaded || m_drawCommands.empty()) return;

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

    // ソート: renderMode → テクスチャポインタ (ステート切替最小化)
    std::sort(m_drawCommands.begin(), m_drawCommands.end(),
        [&services](const ParticleDrawCommand& a, const ParticleDrawCommand& b) {
            if (a.renderMode != b.renderMode) return a.renderMode < b.renderMode;
            auto* ta = services.resources->GetTexture(a.texture);
            auto* tb = services.resources->GetTexture(b.texture);
            return ta < tb;
        });


    int                       curRenderMode = -1;
    ID3D11ShaderResourceView* curTexSRV = reinterpret_cast<ID3D11ShaderResourceView*>(-1);

    for (const auto& cmd : m_drawCommands) {
        GPUParticleBufferSet* buffers = services.resources->GetParticleBuffer(cmd.computeBuffer);
        if (!buffers || !buffers->particleSRV) continue;

        // PSO 切り替え (renderMode が変わった時のみ)
        if (curRenderMode != cmd.renderMode) {
            (cmd.renderMode == 0 ? m_psoAdditive : m_psoTransparent).Apply(dc, rs);
            dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestOnly), 0);
            curRenderMode = cmd.renderMode;
        }

        // 粒子バッファ SRV (★ .Get() を追加)
        ID3D11ShaderResourceView* srvs[] = { buffers->particleSRV.Get() };
        dc->VSSetShaderResources(SLOT_SRV_PARTICLE_BUF, 1, srvs);
        dc->GSSetShaderResources(SLOT_SRV_PARTICLE_BUF, 1, srvs);

        // ★ staticCB(b6) + dynamicCB(b7) を一括バインド (★ .Get() を追加)
        if (buffers->staticCB && buffers->dynamicCB) {
            ID3D11Buffer* cbs[2] = { buffers->staticCB.Get(), buffers->dynamicCB.Get() };
            dc->VSSetConstantBuffers(SLOT_CB_PARTICLE_STATIC, 2, cbs);
            dc->GSSetConstantBuffers(SLOT_CB_PARTICLE_STATIC, 2, cbs);
            dc->PSSetConstantBuffers(SLOT_CB_PARTICLE_STATIC, 2, cbs);
        }

        // テクスチャ (変わった時のみ)
        auto* mainSRV = services.resources->GetTexture(cmd.texture);
        auto* actualSRV = mainSRV ? mainSRV : defaultTexture.Get();
        if (curTexSRV != actualSRV) {
            ID3D11ShaderResourceView* texs[] = { actualSRV };
            dc->PSSetShaderResources(SLOT_SRV_MAT_TEX0, 1, texs);
            curTexSRV = actualSRV;
        }

        dc->Draw(static_cast<UINT>(cmd.maxCount), 0);
    }

    shader->End(dc, services);

    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    dc->VSSetShaderResources(SLOT_SRV_PARTICLE_BUF, 1, nullSRV);
    dc->GSSetShaderResources(SLOT_SRV_PARTICLE_BUF, 1, nullSRV);
    dc->PSSetShaderResources(SLOT_SRV_MAT_TEX0, 1, nullSRV);
    dc->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(nullptr, 0);
}
