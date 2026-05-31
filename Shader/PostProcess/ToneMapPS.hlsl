#include "Shader/PostProcess/Bloom.hlsli"

cbuffer CbToneMap : register(REG_CB(SLOT_CB_TONEMAP))
{
    float gExposure;
    float3 ggPadding;
};

float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(VS_OUT input) : SV_TARGET
{
    float3 hdrColor = gInputTexture.Sample(LinearSampler, input.texcoord).rgb;

    hdrColor *= gExposure;

    float3 sdrColor = ACESFilm(hdrColor);
    sdrColor = pow(sdrColor, float3(0.4545f, 0.4545f, 0.4545f));

    return float4(sdrColor, 1.0f);
}