// ===========================================================================
// 概要: 放射ブラー。中心へ向かう向きにぼかし、外側ほど強く流れて見える速さの演出を出す
//
// 入力: シーンの色
// 出力: ぼかしたシーンの色
//
// 備考: 中心の近くは blurFalloff でぼかしを弱め、真ん中の被写体は読めるまま残す
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneTexture : register(REG_SRV(SLOT_SRV_POST_INPUT0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

#define RADIAL_BLUR_SAMPLES 12

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv       = input.texcoord;
    float2 toCenter = blurCenter - uv;
    float  distFromCenter = length(uv - blurCenter);

    // ぼかす長さ（中心までの距離に対する割合）。中心から離れるほど長い
    float weight = pow(saturate(distFromCenter), blurFalloff) * blurStrength;

    // このピクセルから中心へ向かう線の上を、等間隔に読んで平均する
    float3 col = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < RADIAL_BLUR_SAMPLES; ++i)
    {
        float t = (float) i / (float) (RADIAL_BLUR_SAMPLES - 1);
        float2 sampleUv = saturate(uv + toCenter * (t * weight));
        col += gSceneTexture.Sample(LinearSampler, sampleUv).rgb;
    }
    col /= (float) RADIAL_BLUR_SAMPLES;

    return float4(col, 1.0f);
}
