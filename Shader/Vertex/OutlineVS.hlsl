#include "Shader/Common/Skinning.hlsli"
#include "Shader/Common/Instancing.hlsli"
#include "Shader/Common/Common.hlsli"

// メッシュ用定数バッファ
cbuffer CbMesh : register(b0)
{
    float4 materialColor;
};
// アウトライン用定数バッファ
cbuffer CbOutline : register(b1)
{
    float4 gOutlineColor;
    float gOutlineSize;
    float3 gOutlinePad;
};

VS_OUT main(
    float4 position : POSITION,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    float2 texcoord : TEXCOORD,
    float4 tangent : TANGENT,
    float3 normal : NORMAL,
    uint instanceID : SV_InstanceID
)
{
    VS_OUT vout = (VS_OUT) 0;
    
    float4 skinPos;
    float3 skinNormal;

    // --- 1. ワールド座標と法線の計算 ---
    if (any(boneWeights))
    {
        // スキンメッシュ
        skinPos = SkinningPosition(position, boneWeights, boneIndices);
        skinNormal = SkinningVector(normal, boneWeights, boneIndices);
    }
    else
    {
        // インスタンシング（静的メッシュ）
        float4x4 world = GetInstanceWorld(instanceID);
        skinPos = mul(position, world);
        skinNormal = normalize(mul(float4(normal, 0), world).xyz);
    }

    // --- 2. アウトラインの膨張処理 ---
    // ワールド空間で法線方向に膨らませる
    skinPos.xyz += normalize(skinNormal) * gOutlineSize;

    // --- 3. 座標変換 ---
    vout.vertex = mul(skinPos, viewProjection);

    // その他の情報を埋める
    vout.position = skinPos;
    vout.normal = skinNormal;
    vout.texcoord = texcoord;
    vout.tangent = float4(0, 0, 0, 0); // アウトラインではタンジェント不要
    vout.shadowPos = float4(0, 0, 0, 0);

    return vout;
}