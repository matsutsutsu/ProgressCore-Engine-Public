#pragma once
// ===========================================================================
// 概要: 明るい所をにじませて光って見せるブルームのエフェクト
//
// 責務:
//   - 明るい所を抜き出し、縮小・拡大を繰り返してぼかした光を作る
//   - シーンに光を足した絵を outputRTV に書く（光だけを出すのではない）
//   - 縮小・拡大に使うバッファを持ち、Resize で作り直す
//
// 備考: 明るい所を 1/2 の大きさで抜き出し、縮小と拡大を (kMaxIterations - 1) 回ずつ行って m_upChain[0] に光を作る
//       最後にフル解像度で、シーン + 光 × intensity を outputRTV に書く
// ===========================================================================
#include "IPostEffect.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <memory>

class BloomEffect : public IPostEffect
{
public:
    BloomEffect() = default;
    ~BloomEffect() override;

    void Initialize(ID3D11Device *device, UINT width, UINT height) override;
    void Resize(ID3D11Device *device, UINT width, UINT height) override;

    // inputSRV にブルームをかけ、合成した絵を outputRTV に書く
    void Process(
        ID3D11DeviceContext      *dc,
        ID3D11ShaderResourceView *inputSRV,
        ID3D11RenderTargetView   *outputRTV) override;

    const char *GetName() const override { return "Bloom"; }

    // PostProcessSystem から値を受け取る（texelWidth / texelHeight は Process が入れ直す）
    void UpdateParams(const GpuData::CbBloomData &data) { cbData = data; }

private:
    static constexpr int kMaxIterations = 6; // 縮小するバッファの枚数（1/2, 1/4, ... 1/64）

    GpuData::CbBloomData cbData{};

    struct BloomResources
    {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psExtract;   // 明るい所を抜き出す
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psDown;      // 縮小
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psUp;        // 拡大（1つ大きいバッファと足す）
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psComposite; // シーン + 光
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cbBloom;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<BloomResources> m_res;

    // ブレンドなし。前の描画の加算などのブレンドが残っていても上書きで描くため
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_opaqueBlendState;

    std::vector<RenderTargetHandle> m_downChain; // 縮小した絵（[0] が 1/2）
    std::vector<RenderTargetHandle> m_upChain;   // 拡大しながら足した光（[0] が最後の結果）

    void CreateChains(UINT width, UINT height);
    void ReleaseChains();
    void UpdateCbBloom(ID3D11DeviceContext *dc, const GpuData::CbBloomData &data);
};