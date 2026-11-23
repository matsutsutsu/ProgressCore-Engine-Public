// ===========================================================================
// 概要: SSAO の最後の段。ぼかした遮蔽の値をシーンの色に掛け、隅や隙間を暗くする
//
// 入力: シーンの色・ぼかした遮蔽の値（0〜1）
// 出力: 暗くしたシーンの色
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/ShaderResources.h"

Texture2D gSceneColor : register(REG_SRV(SLOT_SRV_POST_INPUT0));
Texture2D gBlurredAO  : register(REG_SRV(SLOT_SRV_POST_INPUT1));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT input) : SV_TARGET
{
    float3 scene = gSceneColor.Sample(LinearSampler, input.texcoord).rgb;
    float  ao    = gBlurredAO.Sample(LinearSampler, input.texcoord).r;
    return float4(scene * ao, 1.0f);
}
