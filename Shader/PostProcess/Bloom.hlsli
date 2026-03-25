#ifndef BLOOM_HLSLI
#define BLOOM_HLSLI

#include "Shader/Common/RegisterDef.hlsli"

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// 設定用定数バッファ
cbuffer CbBloom : register(REG_CB_BLOOM)
{
    // 抽出用
    float gThreshold; // 閾値 (1.0)
    float gSoftKnee; // 滑らかさ (0.5)
    float gIntensity; // 強度 (1.0)
    float gPadding;

    // テクセルサイズ (1.0 / width, 1.0 / height)
    float2 gTexelSize;
    float2 gPadding2;
};

Texture2D gInputTexture : register(REG_SRV_MATERIAL_TEX0);
// アップサンプリング時は「高解像度側のテクスチャ」も必要
Texture2D gHighResTexture : register(REG_SRV_MATERIAL_TEX1);

SamplerState LinearSampler : register(REG_SMP_LINEAR);


#endif