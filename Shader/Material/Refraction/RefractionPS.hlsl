// Shader/Material/Refraction/RefractionPS.hlsl
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"

Texture2D    gSceneColor : register(REG_SRV(SLOT_SRV_SCENE_COLOR));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 glassTint;
    float  refractStrength;
    float  noiseScale;
    float  noiseSpeed;
    float  _pad;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 screenUV = pin.vertex.xy * gScreenParams.zw;

    float2 offset = normalize(pin.normal).xy;

    if (noiseScale > 0.0f)
    {
        float2 nuv = ScrollUV(pin.texcoord * noiseScale, float2(0.0f, noiseSpeed));
        float  nz = gGlobalNoiseMap.Sample(LinearSampler, nuv).r;
        offset += (nz - 0.5f) * 2.0f;
    }

    float2 distortedUV = saturate(screenUV + offset * refractStrength);
    float3 refracted = gSceneColor.Sample(LinearSampler, distortedUV).rgb;

    return float4(refracted * glassTint.rgb, glassTint.a);
}
