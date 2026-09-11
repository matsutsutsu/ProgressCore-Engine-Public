#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct ToneMapConfigComponent;

// ToneMapEffect: HDR 画像を SDR に焼き付けるためのシェーダー効果クラス
// ToneMapはIPostEffectを継承しない。
// 理由: IPostEffect は「フルスクリーン四角形にシェーダーを適用する」という前提で設計されているが、
// 	 トーンマッピングは「HDRテクスチャを受け取り、SDRテクスチャに書き出す」という処理であり、
//   入力と出力が必ずしもフルスクリーン四角形とは限らないため。
class ToneMapEffect
{
public:
    ToneMapEffect()  = default;
    ~ToneMapEffect() = default;

    void Initialize(ID3D11Device *device);

    // HDR を受け取り、SDR に焼き付ける最終工程
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