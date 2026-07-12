// Shader/Pass/NormalPrepass/NormalPrepassVS.hlsl
//
// 法線プリパス用の頂点シェーダー。ShadowMapVS と同じ「専用の軽量シェーダーで
// 二度描き」パターン(方式B)。出力する法線はビュー空間。
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
