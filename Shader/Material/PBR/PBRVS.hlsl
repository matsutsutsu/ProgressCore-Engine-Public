// ===========================================================================
// 概要: PBR マテリアルの頂点シェーダー。位置・法線・接線をワールドへ移し、画面へ移す
//
// 入力: モデルの頂点（位置・ボーン・UV・接線・法線）とインスタンスの番号
// 出力: VS_OUT（影の座標 shadowPos は使わないので 0）
// ===========================================================================
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Transform.hlsli"

VS_OUT main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float2 texcoord : TEXCOORD,
    float4 tangent : TANGENT,
    float3 normal : NORMAL,
    uint instanceID : SV_InstanceID)
{
    VS_OUT vout = (VS_OUT) 0;

    float3 worldTangent;
    CalculateWorldTransform(
        position, normal, tangent,
        boneWeights, boneIndices, instanceID,
        vout.position, vout.normal, worldTangent
    );
    vout.tangent = float4(worldTangent, tangent.w);

    vout.vertex = mul(vout.position, viewProjection);

    vout.texcoord = texcoord;
    vout.customParams = GetInstanceCustomParams(instanceID);

    return vout;
}