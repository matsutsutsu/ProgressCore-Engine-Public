// ===========================================================================
// 概要: Lambert マテリアルの頂点シェーダー。位置と法線をワールドへ移し、画面へ移す
//
// 入力: モデルの頂点（位置・ボーン・UV・法線）とインスタンスの番号
// 出力: VS_OUT のうち、画面の位置・UV・法線
//
// 備考: VS_OUT のワールド座標（position）は書いていない。LambertPS はそれを読んでライトとフォグを計算する
// ===========================================================================
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Transform.hlsli"




VS_OUT main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float2 texcoord : TEXCOORD,
    float3 normal : NORMAL,
    uint instanceID : SV_InstanceID
)
{
    VS_OUT vout = (VS_OUT) 0;
    float4 worldPos;
    float3 worldNormal;

    CalculateWorldTransform(
        position, normal,
        boneWeights, boneIndices, instanceID,
        worldPos, worldNormal
    );

    vout.normal = worldNormal;

    vout.vertex = mul(worldPos, viewProjection);
    vout.texcoord = texcoord;

    return vout;
}