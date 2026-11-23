// ===========================================================================
// 概要: カラーグレーディング。彩度・コントラスト・明るさ・色味を変える
//
// 入力: シーンの色（HDR）
// 出力: 色を変えたシーンの色
//
// 備考: gradeMixAmount で元の色と混ぜる。0 なら何も変えない
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneTexture : register(REG_SRV(SLOT_SRV_POST_INPUT0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT input) : SV_TARGET
{
    float3 scene = gSceneTexture.Sample(LinearSampler, input.texcoord).rgb;
    float3 col   = scene;

    // 彩度。白黒（Rec.709 の輝度）から元の色へ寄せる。1 で元のまま、0 で白黒
    float luma = dot(col, float3(0.2126f, 0.7152f, 0.0722f));
    col = lerp(float3(luma, luma, luma), col, gradeSaturation);

    // コントラスト。0.5 を中心に明暗の差を広げる。負になった色は 0 にする
    col = (col - 0.5f) * gradeContrast + 0.5f;
    col = max(col, 0.0f);

    col *= gradeBrightness;
    col *= gradeTint.rgb;

    col = lerp(scene, col, saturate(gradeMixAmount));

    return float4(col, 1.0f);
}
