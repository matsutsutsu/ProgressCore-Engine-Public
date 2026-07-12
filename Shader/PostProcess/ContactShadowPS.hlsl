// Shader/PostProcess/ContactShadowPS.hlsl
//
// コンタクトシャドウ(Screen Space Contact Shadow)。
// 深度バッファの中を「光源方向」へ数ステップだけレイマーチし、途中に他の物体が
// あれば遮蔽と判定する。SSAOが「くぼみの遮蔽」を扱うのに対し、これは
// 「直接光が特定方向から遮られるか」を扱う。両者は補完関係。
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Shader/Common/DepthUtils.hlsli"
#include "Shader/PostProcess/ContactShadow.hlsli"

Texture2D    gSceneColor : register(REG_SRV(SLOT_SRV_BLOOM_IN));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

#define CONTACT_SHADOW_STEPS 8

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float3 scene = gSceneColor.Sample(LinearSampler, uv).rgb;

    float eyeDepth = SampleSceneEyeDepth(uv, LinearSampler);

    if (eyeDepth >= csMaxDistance)
        return float4(scene, 1.0f);

    float3 viewPos = ReconstructViewPos(uv, eyeDepth);

    float3 viewLightDir = normalize(mul(float4(-lightDirection.xyz, 0.0f), gView).xyz);

    float3 rayStepView = viewLightDir * (csRayLength / (float)CONTACT_SHADOW_STEPS);

    float shadow = 0.0f;
    float3 samplePos = viewPos;

    [unroll]
    for (int i = 0; i < CONTACT_SHADOW_STEPS; ++i)
    {
        samplePos += rayStepView;

        if (samplePos.z <= 0.0f) break;

        float2 sampleUv = ViewPosToUV(samplePos);

        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f)) break;

        float sceneDepthAtSample = SampleSceneEyeDepth(sampleUv, LinearSampler);

        float diff = samplePos.z - sceneDepthAtSample;

        if (diff > csBias && diff < csThickness)
        {
            shadow = 1.0f;
            break;
        }
    }

    float3 finalColor = scene * (1.0f - shadow * csIntensity);
    return float4(finalColor, 1.0f);
}
