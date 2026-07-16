#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D gInputTexture : register(REG_SRV(SLOT_SRV_PP_DISTORTION_MAP));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;
    
    float2 distortionOffset = float2(0.0f, 0.0f);
    
    // Y軸方向の座標と時間を使って、X軸方向に波打つオフセットを作る
    float waveX = sin(uv.y * 50.0f + time * 10.0f);
    distortionOffset.x = waveX * strength;
    
    float waveY = cos(uv.x * 30.0f + time * 8.0f);
    distortionOffset.y = waveY * (strength * 0.5f);

    // 画面外のサンプリングを防ぐための saturate
    float2 finalUv = saturate(uv + distortionOffset);

    return gInputTexture.Sample(LinearSampler, finalUv);
}