#pragma once
// ===========================================================================
// 概要: HDR の画像を露出と色で調整し、SDR の出力先に書き出すトーンマップ。
//
// 責務:
//   - 全画面の三角形で ToneMapPS を1回かける
//   - ToneMapConfigComponent の値を CbToneMapData に入れて送る
//
// 備考: IPostEffect は継承しない。IPostEffect は2枚の HDR バッファの間で読み書きを入れ替える流れの1段だが、
//       トーンマップは PostProcessManager が最後に1回だけ、SDR の出力先へ書くため。
// ===========================================================================
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct ToneMapConfigComponent;

class ToneMapEffect
{
public:
    ToneMapEffect()  = default;
    ~ToneMapEffect() = default;

    void Initialize(ID3D11Device *device);

    // inputHDR を outputSDR に書く（ビューポートは PostProcessManager が設定済み）
    void Process(ID3D11DeviceContext      *dc,
                 ID3D11ShaderResourceView *inputHDR,
                 ID3D11RenderTargetView   *outputSDR,
                 ToneMapConfigComponent   &config);

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_cbToneMap;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
};