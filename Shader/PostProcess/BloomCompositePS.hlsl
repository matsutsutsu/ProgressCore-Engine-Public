#include "Shader/PostProcess/Bloom.hlsli"

float4 main(VS_OUT input) : SV_TARGET
{
    float3 scene = gInputTexture.Sample(LinearSampler, input.texcoord).rgb;
    float3 glow = gHighResTexture.Sample(LinearSampler, input.texcoord).rgb;

    float3 result = scene + glow;

    if (any(isnan(result)) || any(isinf(result)))
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return float4(result, 1.0f);
}