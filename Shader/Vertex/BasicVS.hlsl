#include "Shader/Common/Skinning.hlsli"
#include "Shader/Common/Instancing.hlsli"
#include "Shader/Common/Common.hlsli"

VS_OUT main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float2 texcoord : TEXCOORD,
    uint instanceID : SV_InstanceID
)
{
    VS_OUT vout = (VS_OUT) 0;

    if (any(boneWeights))
    {
        position = SkinningPosition(position, boneWeights, boneIndices);
    }
    else
    {
        float4x4 world = GetInstanceWorld(instanceID);
        position = mul(position, world);
    }

    vout.vertex = mul(position, viewProjection);
    vout.texcoord = texcoord;

    return vout;
}