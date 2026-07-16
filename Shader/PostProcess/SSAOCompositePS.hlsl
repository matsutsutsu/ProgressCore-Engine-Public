// Shader/PostProcess/SSAOCompositePS.hlsl
// SSAO 第3パス: ぼかし済み遮蔽値をシーン色へ乗算。
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/ShaderResources.h"

Texture2D gSceneColor : register(REG_SRV(SLOT_SRV_BLOOM_IN));
Texture2D gBlurredAO  : register(REG_SRV(SLOT_SRV_BLOOM_HIGHRES));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT input) : SV_TARGET
{
    float3 scene = gSceneColor.Sample(LinearSampler, input.texcoord).rgb;
    float  ao    = gBlurredAO.Sample(LinearSampler, input.texcoord).r;
    return float4(scene * ao, 1.0f);
}
