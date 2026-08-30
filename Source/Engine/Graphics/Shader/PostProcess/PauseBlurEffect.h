/// @file  PauseBlurEffect.h
/// @brief ポーズ中に背景（ゲーム画面）だけをぼかすポストエフェクト。
///
/// 設計方針:
///   - チェーンの最後尾に置く。ブルームや色調補正まで済んだ「完成した絵」をぼかすため。
///   - UI は 2Dパス（ポストプロセスの後）で描かれるので、メニューはぼけない。
///     この順序が「背景だけ引っ込んでメニューが浮く」画を作っている。
///   - 半径の駆動は PostProcessSystem が行う（ポーズ状態は Game 側の関心事で、
///     エフェクト自身が GameFlowState を知る必要はない）。
#pragma once
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class PauseBlurEffect : public IPostEffect
{
public:
    PauseBlurEffect() = default;
    ~PauseBlurEffect() override = default;

    void Initialize(ID3D11Device* device, UINT width, UINT height) override;
    void Resize(ID3D11Device* device, UINT width, UINT height) override;
    void Process(ID3D11DeviceContext* dc,
                 ID3D11ShaderResourceView* inputSRV,
                 ID3D11RenderTargetView* outputRTV) override;

    const char* GetName() const override { return "PauseBlur"; }
    void UpdateParams(const GpuData::CbPauseBlurData& data) { m_cbData = data; }

private:
    GpuData::CbPauseBlurData m_cbData{};

    struct Resources {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<Resources> m_res;
};
