#pragma once
// ===========================================================================
// 概要: ポーズ中にゲームの画面だけをぼかすエフェクト
//
// 責務:
//   - PauseBlurPS を1回かける。半径が 0 なら入力をそのまま写す
//
// 備考: エフェクトの列の最後に置き、ブルームや色の補正まで済んだ絵をぼかす
//       UI はポストプロセスの後に描くので、メニューはぼけずに浮いて見える
//       半径は PostProcessSystem が決める（ポーズかどうかはゲーム側の状態なので、このクラスは知らない）
// ===========================================================================
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class PauseBlurEffect : public IPostEffect
{
public:
    PauseBlurEffect()           = default;
    ~PauseBlurEffect() override = default;

    void Initialize(ID3D11Device *device, UINT width, UINT height) override;
    void Resize(ID3D11Device *device, UINT width, UINT height) override;
    void Process(ID3D11DeviceContext      *dc,
                 ID3D11ShaderResourceView *inputSRV,
                 ID3D11RenderTargetView   *outputRTV) override;

    const char *GetName() const override { return "PauseBlur"; }
    void        UpdateParams(const GpuData::CbPauseBlurData &data) { m_cbData = data; }

private:
    GpuData::CbPauseBlurData m_cbData{};

    struct Resources
    {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<Resources> m_res;
};
