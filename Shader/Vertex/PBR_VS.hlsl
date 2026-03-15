#include "Shader/Common/Skinning.hlsli"
#include "Shader/Common/Instancing.hlsli"
#include "Shader/Common/Common.hlsli"

// PhongVSと全く同じでOKです
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

    // スキニング・インスタンシング判定
    float weightSum = dot(boneWeights, float4(1, 1, 1, 1));
    bool isSkinned = (weightSum > 0.5f);

    if (isSkinned)
    {
        position = SkinningPosition(position, boneWeights, boneIndices);
        vout.normal = SkinningVector(normal, boneWeights, boneIndices);
        vout.tangent.xyz = SkinningVector(tangent.xyz, boneWeights, boneIndices);
    }
    else
    {
        float4x4 world = GetInstanceWorld(instanceID);
        position = mul(position, world);
        vout.normal = normalize(mul(float4(normal, 0), world).xyz);
        vout.tangent.xyz = normalize(mul(float4(tangent.xyz, 0), world).xyz);
    }

    vout.vertex = mul(position, viewProjection);
    vout.texcoord = texcoord;
    vout.tangent.w = tangent.w;
    vout.position = position;
    
    // シャドウマップ座標
    vout.shadowPos = mul(position, light_view_projection);
    
    // インスタンスバッファから customParams を引っこ抜いて封筒に入れる
    vout.customParams = GetInstanceCustomParams(instanceID);

    return vout;
}