// Shader/Common/DepthUtils.hlsli
//
// シーン深度(t4)・法線(t6)を読み、画面空間技術(SSAO/SSR等)の
// 共通ヘルパー。変換ロジックを1箇所に集約し、ドリフトを防ぐ。
//
// 前提: PostProcessManager がポスプロ実行前に t4 をバインドする。
//       NormalPrepass が OpaquePass 後に t6 をバインドする。
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

float3 ReconstructViewPos(float2 uv, float eyeDepth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float2 viewXY = ndc / float2(gProjection._m00, gProjection._m11) * eyeDepth;
    return float3(viewXY, eyeDepth);
}

float2 ViewPosToUV(float3 viewPos)
{
    float2 ndc = float2(viewPos.x * gProjection._m00,
                        viewPos.y * gProjection._m11) / viewPos.z;
    return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
}

Texture2D gSceneNormal : register(REG_SRV(SLOT_SRV_SCENE_NORMAL));

float3 SampleSceneViewNormal(float2 uv, SamplerState smp)
{
    float2 xy = gSceneNormal.SampleLevel(smp, uv, 0).rg;
    // ビュー空間では可視面の法線はカメラ方向(-Z)を向く。RGのみ保持なので Z は負で復元する。
    float  z  = -sqrt(saturate(1.0f - dot(xy, xy)));
    return normalize(float3(xy, z));
}

#endif // DEPTH_UTILS_HLSLI
