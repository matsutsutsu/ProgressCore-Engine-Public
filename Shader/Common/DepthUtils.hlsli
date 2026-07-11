// Shader/Common/DepthUtils.hlsli
//
// シーン深度(t4)を読み、ハードウェア深度(0..1非線形)をビュー空間の距離(ワールド単位)
// へ変換する共有ヘルパー。
#ifndef DEPTH_UTILS_HLSLI
#define DEPTH_UTILS_HLSLI
#include "Shader/Common/Scene.hlsli"

Texture2D gSceneDepth : register(REG_SRV(SLOT_SRV_SCENE_DEPTH));

float LinearEyeDepth(float hwDepth)
{
    return gProjection._m32 / (hwDepth - gProjection._m22);
}

float SampleSceneEyeDepth(float2 uv, SamplerState smp)
{
    float hw = gSceneDepth.SampleLevel(smp, uv, 0).r;
    return LinearEyeDepth(hw);
}

#endif
