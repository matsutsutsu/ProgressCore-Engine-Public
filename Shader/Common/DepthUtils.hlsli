// ===========================================================================
// 概要: シーンの深度と法線を読み、ビュー空間の距離・位置・法線に直す関数
//
// 備考: 画面のピクセルから位置を戻すエフェクト（SSAO・被写界深度など）が使う。直し方をここ1か所にまとめる
//       深度と法線のテクスチャは、PostProcessManager がエフェクトの前に置き、終わったら外す
// ===========================================================================
#ifndef DEPTH_UTILS_HLSLI
#define DEPTH_UTILS_HLSLI
#include "Shader/Common/Scene.hlsli"

Texture2D gSceneDepth : register(REG_SRV(SLOT_SRV_SCENE_DEPTH));

// 深度バッファの値（0〜1）を、カメラからの距離（ビュー空間の z）に直す
float LinearEyeDepth(float hwDepth)
{
    return gProjection._m32 / (hwDepth - gProjection._m22);
}

float SampleSceneEyeDepth(float2 uv, SamplerState smp)
{
    float hw = gSceneDepth.SampleLevel(smp, uv, 0).r;
    return LinearEyeDepth(hw);
}

// 画面の uv と距離から、ビュー空間の位置を戻す
float3 ReconstructViewPos(float2 uv, float eyeDepth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float2 viewXY = ndc / float2(gProjection._m00, gProjection._m11) * eyeDepth;
    return float3(viewXY, eyeDepth);
}

// ビュー空間の位置が、画面のどの uv に映るか
float2 ViewPosToUV(float3 viewPos)
{
    float2 ndc = float2(viewPos.x * gProjection._m00,
                        viewPos.y * gProjection._m11) / viewPos.z;
    return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
}

Texture2D gSceneNormal : register(REG_SRV(SLOT_SRV_SCENE_NORMAL));

// NormalPrepass が書いたビュー空間の法線を読む
float3 SampleSceneViewNormal(float2 uv, SamplerState smp)
{
    float2 xy = gSceneNormal.SampleLevel(smp, uv, 0).rg;
    // テクスチャには XY しかないので Z を戻す。見えている面はカメラの方（-Z）を向くので、Z は負
    float  z  = -sqrt(saturate(1.0f - dot(xy, xy)));
    return normalize(float3(xy, z));
}

#endif // DEPTH_UTILS_HLSLI
