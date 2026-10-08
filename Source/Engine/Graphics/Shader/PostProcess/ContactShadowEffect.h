#pragma once
// ===========================================================================
// 概要: 足元や隙間の小さな影を足すコンタクトシャドウのエフェクト。
//
// 責務:
//   - 深度（t4）を光の方向へ少しずつ進んで調べ（レイマーチ）、遮られていれば暗くする
//   - ContactShadowPS を1回かける。強さが 0 以下なら入力をそのまま写す
//
// 備考: カスケードシャドウの解像度では出ない、接地面のごく近くの影を補う。
//       SSAO の次に置く。深度だけを使い、途中のバッファは持たない。
//       t4 の深度は PostProcessManager::EndScene がバインドする。
// ===========================================================================
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class ContactShadowEffect : public IPostEffect
{
public:
    ContactShadowEffect()           = default;
    ~ContactShadowEffect() override = default;

    void Initialize(ID3D11Device *device, UINT width, UINT height) override;
    void Resize(ID3D11Device *device, UINT width, UINT height) override;

    void Process(ID3D11DeviceContext      *dc,
                 ID3D11ShaderResourceView *inputSRV,
                 ID3D11RenderTargetView   *outputRTV) override;

    const char *GetName() const override { return "ContactShadow"; }

    void UpdateParams(const GpuData::CbContactShadowData &data) { m_cbData = data; }

private:
    GpuData::CbContactShadowData m_cbData{};

    struct Resources
    {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_opaqueBlendState;
    std::unique_ptr<Resources>               m_res;
};
