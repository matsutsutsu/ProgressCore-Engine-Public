#ifndef SHADOWMAP_HLSLI
#define SHADOWMAP_HLSLI

#include "Engine/Graphics/Shader/ShaderResources.h"


// --- データ定義 ---
cbuffer CbShadow : register(REG_CB(SLOT_CB_SHADOW))
{
    row_major float4x4 light_view_projection[3]; // 3層の行列
    float4 cascadeSplits;
    float4 shadow_color;
    float shadow_attenuation;
    float shadow_bias;
    int currentCascadeIndex; // 書き込み用
    float shadow_padding;
};


Texture2DArray ShadowMap : register(REG_SRV(SLOT_SRV_SHADOW));
SamplerComparisonState ShadowSampler : register(REG_SMP(SLOT_SMP_SHADOW));

// --- 関数定義 ---

// 影の重み (0.0=影, 1.0=日向) を計算する
// 引数にワールド座標(worldPos)と、カメラからの距離(viewDepth)をもらう
float CalculateShadowWeight(float4 worldPos, float viewDepth)
{
    // 1. カスケード層（何枚目の紙を使うか）の判定
    int cascadeIndex = 0;
    if (viewDepth > cascadeSplits.y)
        cascadeIndex = 2;
    else if (viewDepth > cascadeSplits.x)
        cascadeIndex = 1;

    // 2. 該当する層の行列を使ってライト空間へ変換
    float4 shadowPos = mul(worldPos, light_view_projection[cascadeIndex]);
    
    float3 shadow_texcoord = shadowPos.xyz / shadowPos.w;
    shadow_texcoord.x = shadow_texcoord.x * 0.5f + 0.5f;
    shadow_texcoord.y = shadow_texcoord.y * -0.5f + 0.5f;

    if (any(shadow_texcoord.xy < 0.0f) || any(shadow_texcoord.xy > 1.0f) || shadow_texcoord.z < 0.0f || shadow_texcoord.z > 1.0f)
        return 1.0f;

    // 3. PCF（ソフトシャドウ）によるサンプリング
    float depth = shadow_texcoord.z - shadow_bias;
    float shadowWeight = 0.0f;
    
    // テクスチャのサイズを自動取得して1ピクセルの幅を計算
    uint width, height, elements;
    ShadowMap.GetDimensions(width, height, elements);
    float2 texelSize = 1.0f / float2(width, height);
    
    // 3x3 (計9回) の周辺サンプリングで境界を滑らかにぼかす
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texelSize;
            shadowWeight += ShadowMap.SampleCmpLevelZero(
                ShadowSampler, float3(shadow_texcoord.xy + offset, cascadeIndex), depth
            );
        }
    }
    shadowWeight /= 9.0f; // 9回サンプリングしたので平均化

    return shadowWeight;
}

// 最終的な色に影を適用する関数
float3 ApplyShadowColor(float3 finalColor, float shadowWeight)
{
    // shadow_color.a (例: 0.5) を「影の濃さ」として使用する
    // 日向(weight=1.0)なら 1.0倍。日陰(weight=0.0)なら 1.0 - 0.5 = 0.5倍の明るさになる
    float intensity = lerp(1.0f - shadow_color.a, 1.0f, shadowWeight);
    return finalColor * intensity;
}

#endif