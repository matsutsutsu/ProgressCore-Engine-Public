// Shader/PostProcess/ChromaticAberrationPS.hlsl
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneTexture : register(REG_SRV(SLOT_SRV_BLOOM_IN));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv  = input.texcoord;
    float2 dir = uv - chromaCenter;
    float  distFromCenter = length(dir);

    float amount = pow(saturate(distFromCenter), chromaFalloff) * chromaStrength;
    float2 offset = dir * amount;

    float r = gSceneTexture.Sample(LinearSampler, saturate(uv + offset)).r;
    float g = gSceneTexture.Sample(LinearSampler, uv).g;
    float b = gSceneTexture.Sample(LinearSampler, saturate(uv - offset)).b;

    return float4(r, g, b, 1.0f);
}
