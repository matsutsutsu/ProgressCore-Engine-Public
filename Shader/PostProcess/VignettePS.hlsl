// Shader/PostProcess/VignettePS.hlsl
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneTexture : register(REG_SRV(SLOT_SRV_BLOOM_IN));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv    = input.texcoord;
    float3 scene = gSceneTexture.Sample(LinearSampler, uv).rgb;

    float dist = distance(uv, float2(0.5f, 0.5f));
    float vig = smoothstep(vigRadius, vigRadius + vigSoftness, dist);

    float pulse = 1.0f;
    if (vigPulseSpeed > 0.0f)
    {
        pulse = 0.6f + 0.4f * (0.5f + 0.5f * sin(vigTime * vigPulseSpeed * 6.2831853f));
    }

    float amount = saturate(vig * vigIntensity * pulse);
    float3 finalColor = lerp(scene, vigColor.rgb, amount);

    return float4(finalColor, 1.0f);
}
