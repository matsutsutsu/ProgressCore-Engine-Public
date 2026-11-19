// ===========================================================================
// 概要: SSAO の2段目。1段目のノイズを、物の境目をまたがないようにぼかす
//
// 入力: 1段目（または横の回）の遮蔽の値・シーンの深度
// 出力: ぼかした遮蔽の値（0〜1）を R8 に書く
//
// 備考: 横と縦の2回に分けて呼ぶ（向きは blurTexelStep で決まる）
//       深度の差が大きい点ほど重みを小さくするので、手前の物の影が奥の壁へにじまない（バイラテラルフィルタ）
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Shader/Common/DepthUtils.hlsli"
#include "Shader/PostProcess/SSAOBlur.hlsli"

Texture2D    gAOInput : register(REG_SRV(SLOT_SRV_POST_INPUT0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

// 中心から片側に読む点の数（両側で 8 点）
#define BLUR_TAPS 4

float main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;

    float centerDepth = SampleSceneEyeDepth(uv, LinearSampler);
    float centerAO    = gAOInput.Sample(LinearSampler, uv).r;

    // 1段目が計算していない遠い所は、ぼかさずに返す
    if (centerDepth >= blurMaxDistance)
        return centerAO;

    float sum = centerAO;
    float weightSum = 1.0f;

    [unroll]
    for (int i = 1; i <= BLUR_TAPS; ++i)
    {
        float2 offset = blurTexelStep * (float)i;

        // 正の側と負の側を1点ずつ読む。重みは深度の差のガウス関数
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
