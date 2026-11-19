// ===========================================================================
// 概要: 法線プリパス（NormalPrepass）の頂点シェーダー。位置を画面へ移し、法線をビュー空間へ移す
//
// 入力: モデルの頂点（ほかのマテリアルと同じ並び）とインスタンスの番号
// 出力: 画面の位置・ビュー空間の法線
//
// 備考: 影と同じく、マテリアルのシェーダーを使わず、この軽いシェーダーでもう一度描く
//       LodDebugPass もこの頂点シェーダーを使う
// ===========================================================================
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Transform.hlsli"

struct NORMAL_VS_OUT
{
    float4 vertex     : SV_POSITION;
    float3 viewNormal : NORMAL;
};

NORMAL_VS_OUT main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float2 texcoord : TEXCOORD,
    float4 tangent : TANGENT,
    float3 normal : NORMAL,
    uint instanceID : SV_InstanceID)
{
    NORMAL_VS_OUT vout = (NORMAL_VS_OUT)0;

    float4 worldPos;
    float3 worldNormal;
    float3 worldTangent;
    CalculateWorldTransform(
        position, normal, tangent,
        boneWeights, boneIndices, instanceID,
        worldPos, worldNormal, worldTangent
    );

    vout.vertex = mul(worldPos, viewProjection);
    vout.viewNormal = normalize(mul(float4(worldNormal, 0.0f), gView).xyz);

    return vout;
}
