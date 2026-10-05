#pragma once
// ===========================================================================
// 概要: GPU パーティクルのエミッタ1つ分の GPU リソース一式。
//
// 責務:
//   - 粒子配列の StructuredBuffer と、その UAV・SRV を持つ
//   - エミッタごとの定数バッファ（静的・動的）を持つ
//
// 備考: ResourceManager のプールが持ち、エミッタは ParticleBufferHandle で参照する。
// ===========================================================================
#include <d3d11.h>
#include <wrl/client.h>

struct GPUParticleBufferSet
{
    Microsoft::WRL::ComPtr<ID3D11Buffer>              particleBuffer; // 粒子配列の StructuredBuffer
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> particleUAV;    // コンピュートシェーダーが書き込む
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  particleSRV;    // ジオメトリシェーダーが読む

    Microsoft::WRL::ComPtr<ID3D11Buffer> staticCB;  // CbParticleStatic。desc が変わったときだけ更新する
    Microsoft::WRL::ComPtr<ID3D11Buffer> dynamicCB; // CbParticleDynamic。毎フレーム更新する
};
