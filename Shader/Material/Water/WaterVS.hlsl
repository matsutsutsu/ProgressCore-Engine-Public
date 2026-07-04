// Shader/Material/Water/WaterVS.hlsl
// Category : unlit(PS側で簡易フレネル)
// PSO      : blend=transparency, depth=testOnly
//
// 通常の座標変換の後、ワールド座標のYを sin波で上下させて波打たせる。
// WaterShared.hlsli の cbuffer をPSと共有しているため、waveAmplitude等は
// MaterialData経由(エディタのAdvanced Parameters)でそのまま調整できる。
//
// 簡略化: 波で頂点を動かした後の法線は再計算していない（平坦なまま）。
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Transform.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"
#include "WaterShared.hlsli"

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

    float t = GetTime();
    float wave =
        sin(vout.position.x * waveFrequency + t * waveSpeed) * 0.6f +
        sin(vout.position.z * waveFrequency * 0.7f - t * waveSpeed * 1.3f) * 0.4f;

    vout.position.y += wave * waveAmplitude;

    vout.vertex = mul(vout.position, viewProjection);
    vout.texcoord = texcoord;
    vout.customParams = GetInstanceCustomParams(instanceID);

    return vout;
}
