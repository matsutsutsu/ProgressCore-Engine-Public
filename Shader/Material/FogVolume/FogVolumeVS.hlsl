// Shader/Material/FogVolume/FogVolumeVS.hlsl
// Category : unlit
// PSO      : blend=transparency, depth=testOnly, cull=none
// Use      : 板ポリ・大きなクワッドに貼る空間用の靄（モデル表面フォグとは独立）
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
    VS_OUT vout = (VS_OUT)0;

    float3 worldTangent;
    CalculateWorldTransform(
        position, normal, tangent,
        boneWeights, boneIndices, instanceID,
        vout.position, vout.normal, worldTangent);
    vout.tangent = float4(worldTangent, tangent.w);

    vout.vertex = mul(vout.position, viewProjection);
    vout.texcoord = texcoord;
    vout.customParams = GetInstanceCustomParams(instanceID);

    return vout;
}
