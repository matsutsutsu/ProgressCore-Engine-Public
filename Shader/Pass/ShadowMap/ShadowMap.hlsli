#ifndef SHADOWMAP_HLSLI
#define SHADOWMAP_HLSLI

// ===========================================================================
// 概要: 平行光源の影（カスケードシャドウ）を読む関数
//
// 備考: 影は3段のカスケード。カメラから cascadeSplits.x / .y の距離で段が切り替わる
//       ShadowPass が書いた深度を、比較サンプラーで3x3 の9点読んで平均する（PCF）
//       影の cbuffer は ShaderShared.h で C++ と共有している
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

Texture2DArray ShadowMap : register(REG_SRV(SLOT_SRV_SHADOW));
SamplerComparisonState ShadowSampler : register(REG_SMP(SLOT_SMP_SHADOW));

// 1つの段の影を読む（0 = 影、1 = 日向）
float SampleCascade(float4 worldPos, int cascadeIndex)
{
    float4 shadowPos = mul(worldPos, light_view_projection[cascadeIndex]);
    float3 shadow_texcoord = shadowPos.xyz / shadowPos.w;

    // ライトから見た画面の位置（-1〜1）を、テクスチャの UV（0〜1、y は下向き）に直す
    shadow_texcoord.x = shadow_texcoord.x * 0.5f + 0.5f;
    shadow_texcoord.y = shadow_texcoord.y * -0.5f + 0.5f;

    // この段の範囲の外は日向として扱う
    if (any(shadow_texcoord.xy < 0.0f) || any(shadow_texcoord.xy > 1.0f) || shadow_texcoord.z < 0.0f || shadow_texcoord.z > 1.0f)
        return 1.0f;

    // 奥の段ほど1テクセルが広いので、バイアスを 3 倍ずつ強める（面に影の縞が出るのを防ぐ）
    float cascadeScale = pow(3.0f, cascadeIndex);
    float dynamicBias = shadow_bias * cascadeScale;

    float depth = shadow_texcoord.z - dynamicBias;
    float shadowWeight = 0.0f;

    uint width, height, elements;
    ShadowMap.GetDimensions(width, height, elements);
    float2 texelSize = 1.0f / float2(width, height);

    // 3x3 の PCF。影の縁をなめらかにする
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
    return shadowWeight / 9.0f;
}

// 影の重み（0 = 影、1 = 日向）。段の境目の手前では次の段と混ぜ、切り替わりの段差を隠す
float CalculateShadowWeight(float4 worldPos, float viewDepth)
{
    int cascadeIndex = 0;
    int nextCascadeIndex = 0;
    float blendFactor = 0.0f;

    // 境目の手前、何 m から次の段と混ぜ始めるか
    const float BLEND_BAND = 5.0f;

    if (viewDepth < cascadeSplits.x)
    {
        cascadeIndex = 0;
        // 境目に近いほど blendFactor が 1 に近づく
        float distToNext = cascadeSplits.x - viewDepth;
        if (distToNext < BLEND_BAND)
        {
            nextCascadeIndex = 1;
            blendFactor = 1.0f - (distToNext / BLEND_BAND);
        }
    }
    else if (viewDepth < cascadeSplits.y)
    {
        cascadeIndex = 1;
        float distToNext = cascadeSplits.y - viewDepth;
        if (distToNext < BLEND_BAND)
        {
            nextCascadeIndex = 2;
            blendFactor = 1.0f - (distToNext / BLEND_BAND);
        }
    }
    else
    {
        cascadeIndex = 2; // 一番奥の段。混ぜる先はない
    }

    float shadowWeight = SampleCascade(worldPos, cascadeIndex);

    // 境目の近くだけ次の段も読む（読む回数が 9 回から 18 回に増えるので、帯の中に限る）
    if (blendFactor > 0.0f)
    {
        float nextShadowWeight = SampleCascade(worldPos, nextCascadeIndex);
        shadowWeight = lerp(shadowWeight, nextShadowWeight, blendFactor);
    }

    return shadowWeight;
}

// 色を影の分だけ暗くする。shadow_color.a が影の濃さ（0.5 なら影の中は半分の明るさ）
float3 ApplyShadowColor(float3 finalColor, float shadowWeight)
{
    float intensity = lerp(1.0f - shadow_color.a, 1.0f, shadowWeight);
    return finalColor * intensity;
}

#endif