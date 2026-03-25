#include "Shader/Common/Skinning.hlsli"
#include "Shader/Common/Instancing.hlsli"
#include "Shader/Common/Common.hlsli"

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

    if (any(boneWeights))
    {
        position = SkinningPosition(position, boneWeights, boneIndices);
        vout.normal = SkinningVector(normal, boneWeights, boneIndices);
    }
    else
    {
        float4x4 world = GetInstanceWorld(instanceID);
        position = mul(position, world);
        vout.normal = normalize(mul(float4(normal, 0), world).xyz);
    }

    vout.vertex = mul(position, viewProjection);
    vout.texcoord = texcoord;

    // Lambertはタンジェントやワールド位置(vout.position)を出力していない構成なのでこれでOK
    // もしPSでそれらを使うように変更した場合は、PhongVSと同様に追加してください

    return vout;
}