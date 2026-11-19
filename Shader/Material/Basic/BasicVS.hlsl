// ===========================================================================
// 概要: Basic マテリアルの頂点シェーダー。位置を画面へ移し、UV を渡す
//
// 入力: モデルの頂点（位置・ボーン・UV）とインスタンスの番号
// 出力: VS_OUT のうち、画面の位置と UV だけ（ほかは 0）
// ===========================================================================
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Transform.hlsli"

VS_OUT main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float2 texcoord : TEXCOORD,
    uint instanceID : SV_InstanceID
)
{
    VS_OUT vout = (VS_OUT) 0;
    float4 worldPos;

    CalculateWorldTransform(
        position, boneWeights, boneIndices, instanceID, worldPos
    );

    vout.vertex = mul(worldPos, viewProjection);
    vout.texcoord = texcoord;

    return vout;
}