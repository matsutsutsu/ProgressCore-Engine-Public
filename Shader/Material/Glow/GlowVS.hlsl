// Shader/Material/Glow/GlowVS.hlsl
//
// 加算発光(Glow)マテリアル用の頂点シェーダー。
// ライティング計算が不要な Unlit 系のため、PBR_VS.hlsl と同じ汎用座標変換のみ行う。
// VS_OUT は共通構造体（Common.hlsli）なので、法線/タンジェントは
// 使わなくても素通しで構わない（PS 側で参照しない）。
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
    VS_OUT vout = (VS_OUT) 0;

    // 1. ワールド空間への変換（スキン/インスタンシング両対応の共通関数）
    float3 worldTangent;
    CalculateWorldTransform(
        position, normal, tangent,
        boneWeights, boneIndices, instanceID,
        vout.position, vout.normal, worldTangent
    );
    vout.tangent = float4(worldTangent, tangent.w);

    // 2. カメラ空間への投影
    vout.vertex = mul(vout.position, viewProjection);

    // 3. その他パラメータ
    vout.texcoord = texcoord;
    vout.customParams = GetInstanceCustomParams(instanceID);

    return vout;
}
