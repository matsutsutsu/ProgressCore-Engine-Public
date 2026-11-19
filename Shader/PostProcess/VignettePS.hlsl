// ===========================================================================
// 概要: ビネット。画面の縁を指定の色で塗り、中心へ目を集める
//
// 入力: シーンの色
// 出力: 縁を塗ったシーンの色
//
// 備考: vigPulseSpeed が 0 より大きいと、塗る濃さが脈打つ（vigTime は PostProcessSystem が進める）
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneTexture : register(REG_SRV(SLOT_SRV_POST_INPUT0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv    = input.texcoord;
    float3 scene = gSceneTexture.Sample(LinearSampler, uv).rgb;

    // 縁の度合い（0〜1）。中心から vigRadius までは 0、そこから vigSoftness の幅で 1 になる
    float dist = distance(uv, float2(0.5f, 0.5f));
    float vig = smoothstep(vigRadius, vigRadius + vigSoftness, dist);

    // 脈打ち。濃さを 0.6〜1.0 倍の間で、1秒に vigPulseSpeed 回揺らす
    float pulse = 1.0f;
    if (vigPulseSpeed > 0.0f)
    {
        pulse = 0.6f + 0.4f * (0.5f + 0.5f * sin(vigTime * vigPulseSpeed * 6.2831853f));
    }

    float amount = saturate(vig * vigIntensity * pulse);
    float3 finalColor = lerp(scene, vigColor.rgb, amount);

    return float4(finalColor, 1.0f);
}
