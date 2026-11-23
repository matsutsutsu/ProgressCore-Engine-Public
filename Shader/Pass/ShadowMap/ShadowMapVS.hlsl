// ===========================================================================
// 概要: 影（ShadowPass）の頂点シェーダー。位置をライトから見た画面へ移す
//
// 入力: モデルの頂点（位置とボーンだけ使う）とインスタンスの番号
// 出力: ライトから見た画面の位置（ピクセルシェーダーはなく、深度だけ書く）
//
// 備考: カスケードの段ごとに1回ずつ描く。どの段かは currentCascadeIndex で受け取る
// ===========================================================================
#include "Shader/Common/Transform.hlsli"
#include "Shader/Pass/ShadowMap/ShadowMap.hlsli"

float4 main(
    float4 position : POSITION,
    float3 normal : NORMAL,
    float4 tangent : TANGENT,
    float2 texcoord : TEXCOORD,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    uint instanceID : SV_InstanceID
) : SV_POSITION
{
    float4 worldPos;
    CalculateWorldTransform(position, boneWeights, boneIndices, instanceID, worldPos);


    return mul(worldPos, light_view_projection[currentCascadeIndex]);
}