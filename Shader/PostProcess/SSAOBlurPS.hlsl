// Shader/PostProcess/SSAOBlurPS.hlsl
// SSAO 第2パス: 深度考慮バイラテラルブラー(H/V共用)。
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Shader/Common/DepthUtils.hlsli"
#include "Shader/PostProcess/SSAOBlur.hlsli"

Texture2D    gAOInput : register(REG_SRV(SLOT_SRV_BLOOM_IN));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

#define BLUR_TAPS 4

float main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;

    float centerDepth = SampleSceneEyeDepth(uv, LinearSampler);
    float centerAO    = gAOInput.Sample(LinearSampler, uv).r;

    if (centerDepth >= blurMaxDistance)
        return centerAO;

    float sum = centerAO;
    float weightSum = 1.0f;

    [unroll]
    for (int i = 1; i <= BLUR_TAPS; ++i)
    {
        float2 offset = blurTexelStep * (float)i;

        {
            float2 sampleUv = uv + offset;
            float sampleDepth = SampleSceneEyeDepth(sampleUv, LinearSampler);
            float depthDiff = abs(sampleDepth - centerDepth);
            float w = exp(-(depthDiff * depthDiff) / max(2.0f * blurDepthSigma * blurDepthSigma, 0.0001f));
            sum += gAOInput.Sample(LinearSampler, sampleUv).r * w;
            weightSum += w;
        }
        {
            float2 sampleUv = uv - offset;
            float sampleDepth = SampleSceneEyeDepth(sampleUv, LinearSampler);
            float depthDiff = abs(sampleDepth - centerDepth);
            float w = exp(-(depthDiff * depthDiff) / max(2.0f * blurDepthSigma * blurDepthSigma, 0.0001f));
            sum += gAOInput.Sample(LinearSampler, sampleUv).r * w;
            weightSum += w;
        }
    }

    return sum / max(weightSum, 0.0001f);
}
