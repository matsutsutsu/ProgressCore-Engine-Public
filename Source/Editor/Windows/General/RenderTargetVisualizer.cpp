#include "Editor/Windows/General/RenderTargetVisualizer.h"
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <algorithm>

namespace
{
    // サムネイル用なので、変換後の幅はここで頭打ちにする（VRAM と GPU の負荷を抑える）
    constexpr uint32_t kMaxBakeWidth = 512;
}

bool RenderTargetVisualizer::EnsureInitialized(ID3D11Device *device)
{
    if (m_initialized) return true;
    if (!device) return false;

    // VS は既存の全画面三角形シェーダーを使う（SV_VertexID から頂点を作るので入力レイアウトは不要）
    if (FAILED(GpuResourceUtils::LoadVertexShader(
            device, "Assets/Shader/FullScreenVS.cso", nullptr, 0, nullptr, m_vs.GetAddressOf())))
        return false;

    if (FAILED(GpuResourceUtils::LoadPixelShader(
            device, "Assets/Shader/RenderTargetDebugPS.cso", m_ps.GetAddressOf())))
        return false;

    if (FAILED(GpuResourceUtils::CreateDynamicConstantBuffer(
            device, sizeof(Cb), m_cb.GetAddressOf())))
        return false;

    // デバッグ表示ではピクセルをそのまま見たいので、補間しない POINT サンプラーにする
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter             = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device->CreateSamplerState(&sd, m_sampler.GetAddressOf())))
        return false;

    // 不透明ブレンド（SSAOEffect 等と同じ）。直前のパスの加算・乗算ブレンドが
    // 残っていると、全画面三角形を描いても意図した色で上書きされず黒く見える
    D3D11_BLEND_DESC bd                      = {};
    bd.RenderTarget[0].BlendEnable           = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&bd, m_opaqueBlendState.GetAddressOf())))
        return false;

    // 深度テストは無効（描画先に DSV を付けないので不要だが、残った状態の影響を受けないよう明示する）
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable              = FALSE;
    dsd.StencilEnable            = FALSE;
    if (FAILED(device->CreateDepthStencilState(&dsd, m_noDepthState.GetAddressOf())))
        return false;

    m_initialized = true;
    return true;
}



RenderTarget *RenderTargetVisualizer::AcquirePoolTarget(ID3D11Device *device, uint32_t w, uint32_t h)
{
    // このフレームの m_poolIndex 番目の RenderTarget を返す（なければ作り、サイズが違えば作り直す）
    if (m_poolIndex >= m_pool.size())
        m_pool.push_back(std::make_unique<RenderTarget>());

    RenderTarget *rt = m_pool[m_poolIndex].get();
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
    // ImGui の DX11 バックエンド（ImGui_ImplDX11_RenderDrawData）と同じく、
    // 変更するステートを丸ごと退避 → 描画 → 丸ごと復元する
    // 個々のハザード（SRV と DSV の同時バインド等）を1つずつ対処するのではなく、触るステージ
    // （IA/VS/PS/OM/RS）をまとめて保存しておくことで、描画後に状態が変わったまま残るのを防ぐ
    struct PipelineStateBackup
    {
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   dsv;
        Microsoft::WRL::ComPtr<ID3D11BlendState>         blendState;
        FLOAT                                            blendFactor[4] = {0, 0, 0, 0};
        UINT                                             sampleMask     = 0;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState>  depthState;
        UINT                                             stencilRef = 0;
        UINT                                             numVP      = 1;
        D3D11_VIEWPORT                                   viewport   = {};
        Microsoft::WRL::ComPtr<ID3D11InputLayout>        inputLayout;
        D3D11_PRIMITIVE_TOPOLOGY                         topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        Microsoft::WRL::ComPtr<ID3D11Buffer>             vb;
        UINT                                             vbStride = 0, vbOffset = 0;
        Microsoft::WRL::ComPtr<ID3D11Buffer>             ib;
        DXGI_FORMAT                                      ibFormat = DXGI_FORMAT_UNKNOWN;
        UINT                                             ibOffset = 0;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>       vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>        ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>             psCb0;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> psSrv0;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> psSrv1;
        Microsoft::WRL::ComPtr<ID3D11SamplerState>       psSampler0;

        void Save(ID3D11DeviceContext *dc)
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
            dc->PSGetConstantBuffers(SLOT_CB_RT_DEBUG, 1, psCb0.GetAddressOf());
            dc->PSGetShaderResources(SLOT_SRV_RT_DEBUG, 1, psSrv0.GetAddressOf());
            dc->PSGetShaderResources(SLOT_SRV_RT_DEBUG_ARRAY, 1, psSrv1.GetAddressOf());
            dc->PSGetSamplers(SLOT_SMP_DEFAULT, 1, psSampler0.GetAddressOf());
        }

        void Restore(ID3D11DeviceContext *dc) const
        {
            dc->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
            dc->OMSetBlendState(blendState.Get(), blendFactor, sampleMask);
            dc->OMSetDepthStencilState(depthState.Get(), stencilRef);
            dc->RSSetViewports(1, &viewport);
            dc->IASetInputLayout(inputLayout.Get());
            dc->IASetPrimitiveTopology(topology);
            ID3D11Buffer *vbPtr = vb.Get();
            dc->IASetVertexBuffers(0, 1, &vbPtr, &vbStride, &vbOffset);
            dc->IASetIndexBuffer(ib.Get(), ibFormat, ibOffset);
            dc->VSSetShader(vs.Get(), nullptr, 0);
            dc->PSSetShader(ps.Get(), nullptr, 0);
            ID3D11Buffer *cbPtr = psCb0.Get();
            dc->PSSetConstantBuffers(SLOT_CB_RT_DEBUG, 1, &cbPtr);
            ID3D11ShaderResourceView *srv0Ptr = psSrv0.Get();
            dc->PSSetShaderResources(SLOT_SRV_RT_DEBUG, 1, &srv0Ptr);
            ID3D11ShaderResourceView *srv1Ptr = psSrv1.Get();
            dc->PSSetShaderResources(SLOT_SRV_RT_DEBUG_ARRAY, 1, &srv1Ptr);
            ID3D11SamplerState *smpPtr = psSampler0.Get();
            dc->PSSetSamplers(SLOT_SMP_DEFAULT, 1, &smpPtr);
        }
    };
}

ID3D11ShaderResourceView *RenderTargetVisualizer::Visualize(ID3D11DeviceContext      *dc,
                                                            ID3D11ShaderResourceView *src, uint32_t w, uint32_t h, const RTVisualizeParams &p)
{
    if (!dc || !src) return src;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    dc->GetDevice(device.GetAddressOf());
    if (!EnsureInitialized(device.Get())) return src;

    // 変換後の解像度（アスペクト比を保ったまま幅を上限で抑える）
    const float    aspect = (w > 0 && h > 0) ? (float)h / (float)w : 9.0f / 16.0f;
    const uint32_t bw     = (w == 0) ? kMaxBakeWidth : (std::min)(w, kMaxBakeWidth);
    const uint32_t bh     = (std::max)(1u, (uint32_t)(bw * aspect));

    RenderTarget *rt = AcquirePoolTarget(device.Get(), bw, bh);
    if (!rt) return src;

    // 全ステートを退避する。個々のリソースが今ほかのステージ（DSV 等）で使われているかに
    // 関係なく、描画後にそのまま元へ戻せる
    PipelineStateBackup backup;
    backup.Save(dc);

    // 出力ステージ（RTV/DSV）を、SRV をバインドする前に空にする
    // GraphicsDevice::SetRenderTargets() はエディタ UI の描画中も深度バッファを
    // DSV としてバインドしたままなので、SceneDepth を表示するとき src がその DSV と
    // 同じリソースになりうる。DSV を外さずに同じリソースを SRV としてバインドすると、
    // D3D11 のハザード検出によって出力のバインドが外され、描画が壊れる
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

    // 全画面三角形を描く設定
    dc->IASetInputLayout(nullptr);
    ID3D11Buffer *nullVB     = nullptr;
    UINT          zeroStride = 0, zeroOffset = 0;
    dc->IASetVertexBuffers(0, 1, &nullVB, &zeroStride, &zeroOffset);
    dc->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(m_vs.Get(), nullptr, 0);
    dc->PSSetShader(m_ps.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(SLOT_CB_RT_DEBUG, 1, m_cb.GetAddressOf());
    ID3D11SamplerState *smp = m_sampler.Get();
    dc->PSSetSamplers(SLOT_SMP_DEFAULT, 1, &smp);
    dc->OMSetBlendState(m_opaqueBlendState.Get(), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(m_noDepthState.Get(), 0);

    // src を、Texture2D と Texture2DArray のうち合う方のスロットにバインドし、もう一方は外す
    ID3D11ShaderResourceView *nullSRV = nullptr;
    if (p.useArray != 0)
    {
        dc->PSSetShaderResources(SLOT_SRV_RT_DEBUG, 1, &nullSRV);
        dc->PSSetShaderResources(SLOT_SRV_RT_DEBUG_ARRAY, 1, &src);
    }
    else
    {
        dc->PSSetShaderResources(SLOT_SRV_RT_DEBUG, 1, &src);
        dc->PSSetShaderResources(SLOT_SRV_RT_DEBUG_ARRAY, 1, &nullSRV);
    }

    rt->Activate(dc); // RTV とビューポートを RenderTarget のサイズに設定する
    dc->Draw(3, 0);

    // 描画後はすぐ SRV と RTV を外してから、退避したステートを復元する
    dc->PSSetShaderResources(SLOT_SRV_RT_DEBUG, 1, &nullSRV);
    dc->PSSetShaderResources(SLOT_SRV_RT_DEBUG_ARRAY, 1, &nullSRV);
    dc->OMSetRenderTargets(0, nullptr, nullptr);

    backup.Restore(dc);

    return rt->GetSRV();
}
