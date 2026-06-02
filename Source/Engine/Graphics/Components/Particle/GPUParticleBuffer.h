#pragma once
#include <d3d11.h>

// =========================================================================
// GPUParticleBufferSet — 1エミッタ分の GPU リソース一式
//
// 旧設計の constantBuffer を staticCB + dynamicCB に分割した。
//   staticCB  : CbParticleStatic  (176 bytes) — Desc 由来・変更時のみ upload
//   dynamicCB : CbParticleDynamic ( 48 bytes) — ランタイム値・毎フレーム upload
//
// ResourceManager::UnloadParticleBuffer() で全フィールドを Release すること。
// =========================================================================
struct GPUParticleBufferSet
{
    ID3D11Buffer* particleBuffer = nullptr;  // StructuredBuffer (粒子配列)
    ID3D11UnorderedAccessView* particleUAV = nullptr;  // CS 書き込み用
    ID3D11ShaderResourceView* particleSRV = nullptr;  // VS 読み取り用

    ID3D11Buffer* staticCB = nullptr;  // CbParticleStatic
    ID3D11Buffer* dynamicCB = nullptr;  // CbParticleDynamic
};