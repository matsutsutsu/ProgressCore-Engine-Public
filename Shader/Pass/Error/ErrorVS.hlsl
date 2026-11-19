// ===========================================================================
// 概要: シェーダーが見つからないメッシュ用の頂点シェーダー。位置を画面へ移すだけ
//
// 入力: モデルの頂点（ほかのマテリアルと同じ並び。位置とボーンだけ使う）とインスタンスの番号
// 出力: 画面の位置
// ===========================================================================
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Transform.hlsli"

float4 main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float2 texcoord : TEXCOORD,
    float4 tangent : TANGENT,
    float3 normal : NORMAL,
    uint instanceID : SV_InstanceID) : SV_POSITION
{
    float4 worldPos;
    CalculateWorldTransform(position, boneWeights, boneIndices, instanceID, worldPos);
    return mul(worldPos, viewProjection);
}