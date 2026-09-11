// 中間バッファを可視化用 RT へ焼く実装（FullScreenVS + RenderTargetDebugPS）。
#include "RenderTargetVisualizer.h"
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include <algorithm>

namespace
{
// サムネイル用途なので bake 解像度はこの幅で頭打ちにする（VRAM/GPU の節約）。
constexpr uint32_t kMaxBakeWidth = 512;
}

bool RenderTargetVisualizer::EnsureInitialized(ID3D11Device* device)
{
    if (m_initialized) return true;
    if (!device)      return false;

    // VS は既存の全画面三角形シェーダーを共用（SV_VertexID ベース、入力レイアウト不要）
    if (FAILED(GpuResourceUtils::LoadVertexShader(
            device, "Assets/Shader/FullScreenVS.cso", nullptr, 0, nullptr, m_vs.GetAddressOf())))
        return false;

    if (FAILED(GpuResourceUtils::LoadPixelShader(
            device, "Assets/Shader/RenderTargetDebugPS.cso", m_ps.GetAddressOf())))
        return false;

    if (FAILED(GpuResourceUtils::CreateDynamicConstantBuffer(
            device, sizeof(Cb), m_cb.GetAddressOf())))
        return false;

    // デバッグ表示は補間で誤魔化さず、ピクセルを正確に見たいので POINT サンプラ。
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device->CreateSamplerState(&sd, m_sampler.GetAddressOf())))
        return false;

    // 不透明ブレンド（SSAOEffect 等と同じ防御）。直前パスの加算/乗算ブレンドが
    // 残っていると、全画面三角形を描いても意図した色で上書きされず黒く見える。
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&bd, m_opaqueBlendState.GetAddressOf())))
        return false;

    // 深度テスト無効（描画先に DSV を付けないため、明示的に無効化して安全側に倒す）。
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable   = FALSE;
    dsd.StencilEnable = FALSE;
    if (FAILED(device->CreateDepthStencilState(&dsd, m_noDepthState.GetAddressOf())))
        return false;

    m_initialized = true;
    return true;
}

RenderTarget* RenderTargetVisualizer::AcquirePoolTarget(ID3D11Device* device, uint32_t w, uint32_t h)
{
    // 今フレームで poolIndex 番目の RT を確保（無ければ作る／サイズ不一致なら作り直す）。
    if (m_poolIndex >= m_pool.size())
        m_pool.push_back(std::make_unique<RenderTarget>());

    RenderTarget* rt = m_pool[m_poolIndex].get();
    ++m_poolIndex;

    if (!rt->IsValid() || rt->GetWidth() != w || rt->GetHeight() != h)
    {
        if (!rt->Create(device, w, h, DXGI_FORMAT_R8G8B8A8_UNORM))
            return nullptr;
    }
    return rt;
}

namespace
{
// ImGui の DX11 バックエンド(ImGui_ImplDX11_RenderDrawData)と同じ思想の
// 「変更するステートを丸ごと退避 → 描画 → 丸ごと復元」ヘルパ。
// 個々のハザード(SRV/DSVの同時バインド等)を都度潰すのではなく、触るステージ
// (IA/VS/PS/OM/RS)をまるごとスナップショットすることで、原因を特定しきれない
// GPU状態の汚染を構造的に防ぐ。
struct PipelineStateBackup
{
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   rtv;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>    dsv;
    Microsoft::WRL::ComPtr<ID3D11BlendState>          blendState;
    FLOAT                                             blendFactor[4] = {0,0,0,0};
    UINT                                               sampleMask = 0;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState>   depthState;
    UINT                                               stencilRef = 0;
    UINT                                               numVP = 1;
    D3D11_VIEWPORT                                    viewport = {};
    Microsoft::WRL::ComPtr<ID3D11InputLayout>         inputLayout;
    D3D11_PRIMITIVE_TOPOLOGY                          topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              vb;
    UINT                                               vbStride = 0, vbOffset = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              ib;
    DXGI_FORMAT                                        ibFormat = DXGI_FORMAT_UNKNOWN;
    UINT                                               ibOffset = 0;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>        vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>         ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              psCb0;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  psSrv0;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  psSrv1;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>        psSampler0;

    void Save(ID3D11DeviceContext* dc)
    {
        dc->OMGetRenderTargets(1, rtv.GetAddressOf(), dsv.GetAddressOf());
        dc->OMGetBlendState(blendState.GetAddressOf(), blendFactor, &sampleMask);
        dc->OMGetDepthStencilState(depthState.GetAddressOf(), &stencilRef);
        dc->RSGetViewports(&numVP, &viewport);
        dc->IAGetInputLayout(inputLayout.GetAddressOf());
        dc->IAGetPrimitiveTopology(&topology);
        dc->IAGetVertexBuffers(0, 1, vb.GetAddressOf(), &vbStride, &vbOffset);
        dc->IAGetIndexBuffer(ib.GetAddressOf(), &ibFormat, &ibOffset);
        dc->VSGetShader(vs.GetAddressOf(), nullptr, nullptr);
        dc->PSGetShader(ps.GetAddressOf(), nullptr, nullptr);
        dc->PSGetConstantBuffers(0, 1, psCb0.GetAddressOf());
        dc->PSGetShaderResources(0, 1, psSrv0.GetAddressOf());
        dc->PSGetShaderResources(1, 1, psSrv1.GetAddressOf());
        dc->PSGetSamplers(0, 1, psSampler0.GetAddressOf());
    }

    void Restore(ID3D11DeviceContext* dc) const
    {
        dc->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
        dc->OMSetBlendState(blendState.Get(), blendFactor, sampleMask);
        dc->OMSetDepthStencilState(depthState.Get(), stencilRef);
        dc->RSSetViewports(1, &viewport);
        dc->IASetInputLayout(inputLayout.Get());
        dc->IASetPrimitiveTopology(topology);
        ID3D11Buffer* vbPtr = vb.Get();
        dc->IASetVertexBuffers(0, 1, &vbPtr, &vbStride, &vbOffset);
        dc->IASetIndexBuffer(ib.Get(), ibFormat, ibOffset);
        dc->VSSetShader(vs.Get(), nullptr, 0);
        dc->PSSetShader(ps.Get(), nullptr, 0);
        ID3D11Buffer* cbPtr = psCb0.Get();
        dc->PSSetConstantBuffers(0, 1, &cbPtr);
        ID3D11ShaderResourceView* srv0Ptr = psSrv0.Get();
        dc->PSSetShaderResources(0, 1, &srv0Ptr);
        ID3D11ShaderResourceView* srv1Ptr = psSrv1.Get();
        dc->PSSetShaderResources(1, 1, &srv1Ptr);
        ID3D11SamplerState* smpPtr = psSampler0.Get();
        dc->PSSetSamplers(0, 1, &smpPtr);
    }
};
} // namespace

ID3D11ShaderResourceView* RenderTargetVisualizer::Visualize(ID3D11DeviceContext* dc,
    ID3D11ShaderResourceView* src, uint32_t w, uint32_t h, const RTVisualizeParams& p)
{
    if (!dc || !src) return src;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    dc->GetDevice(device.GetAddressOf());
    if (!EnsureInitialized(device.Get())) return src;

    // bake 解像度（アスペクト維持で幅をクランプ）
    const float aspect = (w > 0 && h > 0) ? (float)h / (float)w : 9.0f / 16.0f;
    const uint32_t bw = (w == 0) ? kMaxBakeWidth : (std::min)(w, kMaxBakeWidth);
    const uint32_t bh = (std::max)(1u, (uint32_t)(bw * aspect));

    RenderTarget* rt = AcquirePoolTarget(device.Get(), bw, bh);
    if (!rt) return src;

    // ImGui バックエンドと同じ「全ステート退避」。個々のリソースが今この瞬間
    // 別のステージ(DSV等)で使用中かどうかを気にせず、後で寸分違わず元に戻す。
    PipelineStateBackup backup;
    backup.Save(dc);

    // 重要: 出力ステージ(RTV/DSV)を SRV バインドより前に完全に空にする。
    // GraphicsDevice::SetRenderTargets() はエディタUI描画中も深度バッファを
    // DSV として常時バインドし続けており、SceneDepth を可視化する際 src が
    // その DSV と "同一リソース" になりうる。DSV を外さないまま同じリソースを
    // SRV として束ねると D3D11 のハザード検出に触れ、出力バインドが強制的に
    // 無効化されてパイプライン状態が壊れる。
    dc->OMSetRenderTargets(0, nullptr, nullptr);

    // 定数バッファ更新
    Cb cb{};
    cb.mode          = p.mode;
    cb.exposure      = p.exposure;
    cb.nearZ         = p.nearZ;
    cb.farZ          = p.farZ;
    cb.arraySlice    = p.arraySlice;
    cb.useArray      = p.useArray;
    cb.channelSelect = p.channelSelect;
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &cb, sizeof(cb));
        dc->Unmap(m_cb.Get(), 0);
    }

    // パイプライン設定（全画面三角形）
    dc->IASetInputLayout(nullptr);
    ID3D11Buffer* nullVB = nullptr; UINT zeroStride = 0, zeroOffset = 0;
    dc->IASetVertexBuffers(0, 1, &nullVB, &zeroStride, &zeroOffset);
    dc->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(m_vs.Get(), nullptr, 0);
    dc->PSSetShader(m_ps.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    ID3D11SamplerState* smp = m_sampler.Get();
    dc->PSSetSamplers(0, 1, &smp);
    dc->OMSetBlendState(m_opaqueBlendState.Get(), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(m_noDepthState.Get(), 0);

    // ソースを t0(Texture2D) か t1(Texture2DArray) に束ねる
    ID3D11ShaderResourceView* nullSRV = nullptr;
    if (p.useArray != 0)
    {
        dc->PSSetShaderResources(0, 1, &nullSRV);
        dc->PSSetShaderResources(1, 1, &src);
    }
    else
    {
        dc->PSSetShaderResources(0, 1, &src);
        dc->PSSetShaderResources(1, 1, &nullSRV);
    }

    rt->Activate(dc);       // RTV + ビューポートを RT サイズに設定
    dc->Draw(3, 0);

    // 描画後は即座に SRV/RTV を解除してから、退避した全ステートを復元する。
    dc->PSSetShaderResources(0, 1, &nullSRV);
    dc->PSSetShaderResources(1, 1, &nullSRV);
    dc->OMSetRenderTargets(0, nullptr, nullptr);

    backup.Restore(dc);

    return rt->GetSRV();
}
