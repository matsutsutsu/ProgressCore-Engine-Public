#pragma once
#include <d3d11.h>
#include <wrl/client.h>

// GPU パーティクルのエミッタ1つ分の GPU リソース（粒子の StructuredBuffer と定数バッファ）
// ResourceManager のプールが持ち、エミッタは ParticleBufferHandle で参照する
struct GPUParticleBufferSet
{
    Microsoft::WRL::ComPtr<ID3D11Buffer>              particleBuffer; // 粒子配列の StructuredBuffer
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> particleUAV;    // コンピュートシェーダーが書き込む
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  particleSRV;    // ジオメトリシェーダーが読む

    Microsoft::WRL::ComPtr<ID3D11Buffer> staticCB;  // CbParticleStatic。desc が変わったときだけ更新する
    Microsoft::WRL::ComPtr<ID3D11Buffer> dynamicCB; // CbParticleDynamic。毎フレーム更新する
};
