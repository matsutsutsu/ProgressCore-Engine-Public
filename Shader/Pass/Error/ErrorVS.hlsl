// Shader/Core/ErrorVS.hlsl
// 未登録シェーダーのフォールバック用 VS。標準モデル頂点を viewProjection で変換するだけ。
#include "Shader/Common/Common.hlsli"      // viewProjection (CbScene)
#include "Shader/Common/Transform.hlsli"   // CalculateWorldTransform

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