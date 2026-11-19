// ===========================================================================
// 概要: 被写界深度。ピントの合う距離から離れたピクセルほど大きくぼかす
//
// 入力: シーンの色・シーンの深度
// 出力: ぼかしたシーンの色
//
// 備考: ぼかしは円盤状に 12 点を取って平均するだけ。手前と奥のぼけを分けて扱わない
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Shader/Common/DepthUtils.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneColor : register(REG_SRV(SLOT_SRV_POST_INPUT0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

// 読む点の並び。半径 0.5 と 1.0 の2つの輪に6点ずつ（ぼけの半径を掛けて使う）
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

    // ぼけの割合（0〜1）。ピントの前後 dofFocusRange の内側は 0、そこから dofFalloff 離れると 1
    float coc = saturate((abs(eyeDepth - dofFocusDistance) - dofFocusRange) / max(dofFalloff, 0.0001f));
    float radius = coc * dofMaxBlur;

    // ぼけがほとんどないピクセルは、そのまま返して読む回数を減らす
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
