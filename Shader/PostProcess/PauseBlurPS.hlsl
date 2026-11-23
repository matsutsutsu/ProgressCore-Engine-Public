// ===========================================================================
// 概要: ポーズ中のぼかし。ゲームの画面だけをぼかし、奥へ引っ込んだように見せる
//
// 入力: シーンの色
// 出力: ぼかしたシーンの色
//
// 備考: UI はこの後の 2D パスで描くので、メニューはぼけない
//       縮小を重ねるぼかしにせず、1回で 16 点を読む。ポーズ中にしか走らないので、
//       速さより、レンダーターゲットと手順を増やさないことを選んだ
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneColor   : register(REG_SRV(SLOT_SRV_POST_INPUT0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

// 読む点の並び。2つの輪に8点ずつ。内側の輪を 22.5 度回して、外側の点の間を埋める
#define PAUSE_BLUR_TAPS 16
static const float2 kRing[PAUSE_BLUR_TAPS] = {
    // 外側（半径 1.0）
    float2( 1.000,  0.000), float2( 0.707,  0.707), float2( 0.000,  1.000), float2(-0.707,  0.707),
    float2(-1.000,  0.000), float2(-0.707, -0.707), float2( 0.000, -1.000), float2( 0.707, -0.707),
    // 内側（半径 0.5）
    float2( 0.462,  0.191), float2( 0.191,  0.462), float2(-0.191,  0.462), float2(-0.462,  0.191),
    float2(-0.462, -0.191), float2(-0.191, -0.462), float2( 0.191, -0.462), float2( 0.462, -0.191)
};

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv     = input.texcoord;
    float3 center = gSceneColor.Sample(LinearSampler, uv).rgb;

    // 半径がほぼ 0 ならそのまま返す。ポーズを解いて半径が縮んでいく途中も、ここで元の画面に戻る
    if (pauseBlurRadius < 0.0002f)
        return float4(center, 1.0f);

    float3 sum   = center;
    float  count = 1.0f;

    [unroll]
    for (int i = 0; i < PAUSE_BLUR_TAPS; ++i)
    {
        sum   += gSceneColor.Sample(LinearSampler, uv + kRing[i] * pauseBlurRadius).rgb;
        count += 1.0f;
    }

    return float4(sum / count, 1.0f);
}
