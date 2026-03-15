#include "Shader/Common/Skinning.hlsli"
#include "Shader/Common/Instancing.hlsli"
#include "Shader/Pass/ShadowMap/ShadowMap.hlsli"

float4 main(
    float4 position : POSITION,
    float3 normal : NORMAL,
    float4 tangent : TANGENT,
    float2 texcoord : TEXCOORD,
    float4 boneWeights : BONE_WEIGHTS,
    uint4 boneIndices : BONE_INDICES,
    uint instanceID : SV_InstanceID
) : SV_POSITION
{
    float4 worldPos;

    // --- 分岐判定 ---
    // ボーンウェイトの合計を見て、スキンメッシュか静的インスタンスかを判断
    float weightSum = dot(boneWeights, float4(1, 1, 1, 1));
    bool isSkinned = (weightSum > 0.5f);

    if (isSkinned)
    {
        // [スキンメッシュ] (Mr.Incredibleなど)
        // CbSkeleton (b12) を使用して変形
        worldPos = SkinningPosition(position, boneWeights, boneIndices);
    }
    else
    {
        // [インスタンシング] (大量の球体や背景など)
        // InstanceBuffer (t11) から行列を取得して変形
        float4x4 world = GetInstanceWorld(instanceID);
        worldPos = mul(position, world);
    }
    
    // ライトから見た座標系に変換 (World -> LightClip)
    return mul(worldPos, light_view_projection);
}