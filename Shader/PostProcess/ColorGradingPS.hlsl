// Shader/PostProcess/ColorGradingPS.hlsl
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneTexture : register(REG_SRV(SLOT_SRV_BLOOM_IN));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT input) : SV_TARGET
{
    float3 scene = gSceneTexture.Sample(LinearSampler, input.texcoord).rgb;
    float3 col   = scene;

    float luma = dot(col, float3(0.2126f, 0.7152f, 0.0722f));
    col = lerp(float3(luma, luma, luma), col, gradeSaturation);

    col = (col - 0.5f) * gradeContrast + 0.5f;
    col = max(col, 0.0f);

    col *= gradeBrightness;
    col *= gradeTint.rgb;

    col = lerp(scene, col, saturate(gradeMixAmount));

    return float4(col, 1.0f);
}
