// Shader/PostProcess/DepthOfFieldPS.hlsl
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Shader/Common/DepthUtils.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneColor : register(REG_SRV(SLOT_SRV_BLOOM_IN));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

#define DOF_TAPS 12
static const float2 kDisk[DOF_TAPS] = {
    float2( 0.500,  0.000), float2( 0.250,  0.433), float2(-0.250,  0.433),
    float2(-0.500,  0.000), float2(-0.250, -0.433), float2( 0.250, -0.433),
    float2( 0.866,  0.500), float2( 0.000,  1.000), float2(-0.866,  0.500),
    float2(-0.866, -0.500), float2( 0.000, -1.000), float2( 0.866, -0.500)
};

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float3 centerColor = gSceneColor.Sample(LinearSampler, uv).rgb;

    float eyeDepth = SampleSceneEyeDepth(uv, LinearSampler);

    float coc = saturate((abs(eyeDepth - dofFocusDistance) - dofFocusRange) / max(dofFalloff, 0.0001f));
    float radius = coc * dofMaxBlur;

    if (radius < 0.0005f)
        return float4(centerColor, 1.0f);

    float3 sum = centerColor;
    float  count = 1.0f;
    [unroll]
    for (int i = 0; i < DOF_TAPS; ++i)
    {
        float2 sampleUv = uv + kDisk[i] * radius;
        sum += gSceneColor.Sample(LinearSampler, sampleUv).rgb;
        count += 1.0f;
    }

    return float4(sum / count, 1.0f);
}
