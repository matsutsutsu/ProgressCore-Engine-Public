#pragma once
#include <d3d11.h>
#include <wrl/client.h> // これを忘れないように

// =========================================================================
// GPUParticleBufferSet — 1エミッタ分の GPU リソース一式
// =========================================================================
struct GPUParticleBufferSet
{
    // 全てを生ポインタから ComPtr に変更！
    Microsoft::WRL::ComPtr<ID3D11Buffer>              particleBuffer; // StructuredBuffer (粒子配列)
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> particleUAV;    // CS 書き込み用
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  particleSRV;    // VS 読み取り用

    Microsoft::WRL::ComPtr<ID3D11Buffer>              staticCB;       // CbParticleStatic
    Microsoft::WRL::ComPtr<ID3D11Buffer>              dynamicCB;      // CbParticleDynamic
};