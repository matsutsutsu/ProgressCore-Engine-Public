// Shader/PostProcess/RadialBlurPS.hlsl
//
// 放射状ブラー(集中線)ポストエフェクト。
// 画面中心(center)へ向かってUVをずらしながら複数回サンプルし平均することで、
// 外周ほど強く流れる「スピード感」を出す。
//
// center付近は鮮明に保つ(falloffで制御)ため、中心の被写体は読めるまま周囲だけが流れる。
#include "Shader/Common/Common.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneTexture : register(REG_SRV(SLOT_SRV_BLOOM_IN));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

#define RADIAL_BLUR_SAMPLES 12

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv       = input.texcoord;
    float2 toCenter = blurCenter - uv;
    float  distFromCenter = length(uv - blurCenter);

    float weight = pow(saturate(distFromCenter), blurFalloff) * blurStrength;

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
