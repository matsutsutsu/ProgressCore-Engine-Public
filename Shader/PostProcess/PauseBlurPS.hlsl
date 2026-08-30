// Shader/PostProcess/PauseBlurPS.hlsl
//   ポーズ中に背景（ゲーム画面）だけをぼかす。UIは2Dパスでこの後に描かれるので、
//   メニューは鮮明なまま「奥のゲームだけが引っ込む」画になる。
//
//   ダウンサンプル連鎖を組まず1パスでタップ数を稼いでいる理由:
//     このエフェクトはポーズ中しか走らない＝ゲームが止まっている間の負荷なので、
//     速度より「追加のレンダーターゲットと状態管理を増やさないこと」を優先した。
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneColor   : register(REG_SRV(SLOT_SRV_BLOOM_IN));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

// 二重の円環。内側を半径0.5でずらして配置し、単純な円1本より隙間を埋める。
#define PAUSE_BLUR_TAPS 16
static const float2 kRing[PAUSE_BLUR_TAPS] = {
    // 外周（半径 1.0）
    float2( 1.000,  0.000), float2( 0.707,  0.707), float2( 0.000,  1.000), float2(-0.707,  0.707),
    float2(-1.000,  0.000), float2(-0.707, -0.707), float2( 0.000, -1.000), float2( 0.707, -0.707),
    // 内周（半径 0.5・22.5度ずらし）
    float2( 0.462,  0.191), float2( 0.191,  0.462), float2(-0.191,  0.462), float2(-0.462,  0.191),
    float2(-0.462, -0.191), float2(-0.191, -0.462), float2( 0.191, -0.462), float2( 0.462, -0.191)
};

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv     = input.texcoord;
    float3 center = gSceneColor.Sample(LinearSampler, uv).rgb;

    // 半径がほぼ0なら素通し。ポーズ解除の途中でも分岐で自然に元へ戻る。
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
