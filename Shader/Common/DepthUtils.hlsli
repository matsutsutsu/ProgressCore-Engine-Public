// Shader/Common/DepthUtils.hlsli
//
// シーン深度(t4)を読み、ハードウェア深度(0..1非線形)をビュー空間の距離(ワールド単位)
// へ変換する共有ヘルパー。DoF・SSAO・屈折のソフト化・X線・フォグ改善など、
// 今後の深度利用はすべてこのファイル経由にする(変換ロジックを1箇所に集約し、ドリフトを防ぐ)。
//
// 前提: PostProcessManager がポスプロ実行前に、シーン深度SRVを t4 にバインドする。
//       深度は不透明描画で書かれた値(半透明はTestOnlyで書かないのが通常)。
#ifndef DEPTH_UTILS_HLSLI
#define DEPTH_UTILS_HLSLI
#include "Shader/Common/Scene.hlsli" // CbScene(gProjection) を使う

// シーン深度テクスチャ(t4)。R成分にハードウェア深度が入る。
Texture2D gSceneDepth : register(REG_SRV(SLOT_SRV_SCENE_DEPTH));

// ---------------------------------------------------------------------------
// ハードウェア深度(0..1) → ビュー空間Z(カメラからの距離, ワールド単位)。
//   DirectX(LH, 深度0..1)の透視投影を前提とする。
//   ndc.z = (viewZ*P[2][2] + P[3][2]) / viewZ  →  viewZ = P[3][2] / (ndc.z - P[2][2])
//   row_major の gProjection に対し ._m32=行3列2(P[3][2]), ._m22=行2列2(P[2][2])。
//   ※もし手前/奥が反転して見えたら、投影の符号規約が異なるので
//     分母を (P[2][2] - hwDepth) に入れ替えて検証すること。
// ---------------------------------------------------------------------------
float LinearEyeDepth(float hwDepth)
{
    return gProjection._m32 / (hwDepth - gProjection._m22);
}

// 指定UVのシーン深度をビュー距離(ワールド単位)で返す。
float SampleSceneEyeDepth(float2 uv, SamplerState smp)
{
    float hw = gSceneDepth.SampleLevel(smp, uv, 0).r;
    return LinearEyeDepth(hw);
}

// ---------------------------------------------------------------------------
// スクリーンUV + ビュー深度 → ビュー空間座標を復元。
//   SSAO / SSR / コンタクトシャドウ など、画面空間で3D的に計算する技術が共通で使う。
//   gProjection._m00/_m11 は焦点距離(FOVとアスペクトから決まる)。
// ---------------------------------------------------------------------------
float3 ReconstructViewPos(float2 uv, float eyeDepth)
{
    // NDC(-1..1)へ。Yは反転(UVは下向き正、NDCは上向き正)。
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float2 viewXY = ndc / float2(gProjection._m00, gProjection._m11) * eyeDepth;
    return float3(viewXY, eyeDepth);
}

// ビュー空間座標 → スクリーンUV へ再投影(ReconstructViewPos の逆変換)。
float2 ViewPosToUV(float3 viewPos)
{
    float2 ndc = float2(viewPos.x * gProjection._m00,
                        viewPos.y * gProjection._m11) / viewPos.z;
    return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
}

#endif // DEPTH_UTILS_HLSLI
