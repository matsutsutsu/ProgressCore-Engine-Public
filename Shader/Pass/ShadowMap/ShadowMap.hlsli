#ifndef SHADOWMAP_HLSLI
#define SHADOWMAP_HLSLI

#include "Shader/Common/RegisterDef.hlsli"


// --- データ定義 ---
cbuffer CbShadow : register(REG_CB_SHADOW)
{
    row_major float4x4 light_view_projection;
    float4 shadow_color;
    float shadow_attenuation;
    float shadow_bias;
    float2 shadow_dummy;
};


Texture2D ShadowMap : register(REG_SRV_SHADOW);
SamplerComparisonState ShadowSampler : register(REG_SMP_SHADOW);

// --- 関数定義 ---

// 影の重み (0.0=影, 1.0=日向) を計算する
float CalculateShadowWeight(float4 shadowPos)
{
    
    
    float3 shadow_texcoord = shadowPos.xyz / shadowPos.w;
    shadow_texcoord.x = shadow_texcoord.x * 0.5f + 0.5f;
    shadow_texcoord.y = shadow_texcoord.y * -0.5f + 0.5f;

    // 範囲外判定
    if (any(shadow_texcoord.xy < 0.0f) || any(shadow_texcoord.xy > 1.0f) ||
        shadow_texcoord.z < 0.0f || shadow_texcoord.z > 1.0f)
    {
        return 1.0f;
    }

    // PCF ロジック
    float shadowWeight = 0.0f;
    float width, height;
    ShadowMap.GetDimensions(width, height);
    float2 texelSize = 1.0f / float2(width, height);
    
    int pixelSizeInt = 1; // サンプリングする範囲（1なら3x3、2なら5x5）
    float sampleCount = 0.0f; // サンプリングした数のカウント

    [unroll]
    for (int y = -pixelSizeInt; y <= pixelSizeInt; ++y)
    {
        [unroll]
        for (int x = -pixelSizeInt; x <= pixelSizeInt; ++x)
        {
            float2 offset = float2(x, y) * texelSize;
            shadowWeight += ShadowMap.SampleCmpLevelZero(
                ShadowSampler, shadow_texcoord.xy + offset, shadow_texcoord.z - shadow_bias
            );
            sampleCount += 1.0f; // サンプリングした数をカウント

        }
    }
    shadowWeight /= sampleCount;
    
    return max(shadowWeight, shadow_attenuation);
}

// 最終的な色に影を適用する関数
float3 ApplyShadowColor(float3 finalColor, float shadowWeight)
{
    float3 coloredShadow = finalColor * shadow_color.rgb;
    return lerp(coloredShadow, finalColor, shadowWeight);
}


#endif