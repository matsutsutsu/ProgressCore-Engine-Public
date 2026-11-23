// ===========================================================================
// 概要: 頂点をワールド空間へ動かす関数。スキンメッシュとインスタンシングの両方を1つの入口で扱う
//
// 備考: ボーンの重みの合計で見分ける。重みがあればスキンメッシュ（ボーンの行列）、なければ
//       インスタンスのワールド行列を使う。必要な出力の数に合わせて3つの形がある
// ===========================================================================
#ifndef TRANSFORM_HLSLI
#define TRANSFORM_HLSLI

#include "Shader/Common/Skinning.hlsli"
#include "Shader/Common/Instancing.hlsli"

// 位置・法線・接線（Phong・PBR など、法線マップを使うマテリアル用）
void CalculateWorldTransform(
    in float4 localPos, in float3 localNormal, in float4 localTangent,
    in float4 boneWeights, in uint4 boneIndices, in uint instanceID,
    out float4 outWorldPos, out float3 outWorldNormal, out float3 outWorldTangent)
{
    float weightSum = dot(boneWeights, float4(1, 1, 1, 1));
    if (weightSum > 0.5f)
    {
        // スキンメッシュ
        outWorldPos = SkinningPosition(localPos, boneWeights, boneIndices);
        outWorldNormal = SkinningVector(localNormal, boneWeights, boneIndices);
        outWorldTangent = SkinningVector(localTangent.xyz, boneWeights, boneIndices);
    }
    else
    {
        // インスタンシング（ボーンのないメッシュ）
        float4x4 world = GetInstanceWorld(instanceID);
        outWorldPos = mul(localPos, world);
        outWorldNormal = normalize(mul(float4(localNormal, 0), world).xyz);
        outWorldTangent = normalize(mul(float4(localTangent.xyz, 0), world).xyz);
    }
}

// 位置・法線（Lambert 用）
void CalculateWorldTransform(
    in float4 localPos, in float3 localNormal,
    in float4 boneWeights, in uint4 boneIndices, in uint instanceID,
    out float4 outWorldPos, out float3 outWorldNormal)
{
    float weightSum = dot(boneWeights, float4(1, 1, 1, 1));
    if (weightSum > 0.5f)
    {
        outWorldPos = SkinningPosition(localPos, boneWeights, boneIndices);
        outWorldNormal = SkinningVector(localNormal, boneWeights, boneIndices);
    }
    else
    {
        float4x4 world = GetInstanceWorld(instanceID);
        outWorldPos = mul(localPos, world);
        outWorldNormal = normalize(mul(float4(localNormal, 0), world).xyz);
    }
}

// 位置だけ（Basic・影用）
void CalculateWorldTransform(
    in float4 localPos,
    in float4 boneWeights, in uint4 boneIndices, in uint instanceID,
    out float4 outWorldPos)
{
    float weightSum = dot(boneWeights, float4(1, 1, 1, 1));
    if (weightSum > 0.5f)
    {
        outWorldPos = SkinningPosition(localPos, boneWeights, boneIndices);
    }
    else
    {
        float4x4 world = GetInstanceWorld(instanceID);
        outWorldPos = mul(localPos, world);
    }
}

#endif // TRANSFORM_HLSLI