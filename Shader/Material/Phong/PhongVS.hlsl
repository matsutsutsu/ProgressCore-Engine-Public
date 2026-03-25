#include "Shader/Common/Skinning.hlsli"
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Instancing.hlsli"

VS_OUT main(
	float4 position : POSITION,
	float4 boneWeights : BONE_WEIGHTS,
	uint4 boneIndices : BONE_INDICES,
	float2 texcoord : TEXCOORD,
	float4 tangent : TANGENT,
	float3 normal		: NORMAL,
	uint instanceID : SV_InstanceID
)
{
	VS_OUT vout = (VS_OUT)0;

    
    // ゴミデータ対策として、重みの合計がある程度(0.5以上など)ないとスキニングとみなさない
    float weightSum = dot(boneWeights, float4(1, 1, 1, 1));
    bool isSkinned = (weightSum > 0.5f);
    //bool isSkinned = false;
    
    // --- 座標変換ロジック ---
    // ボーンウェイトの合計が0より大きいならスキンメッシュとみなす
    if (isSkinned)
    {
        // [スキンメッシュパス]
        // CbSkeleton (b12) のボーン行列を使って変形
        // ※このパスでは instanceID は 0 になる想定ですが、使わないので問題ありません
        position = SkinningPosition(position, boneWeights, boneIndices);
        vout.normal = SkinningVector(normal, boneWeights, boneIndices);
        vout.tangent.xyz = SkinningVector(tangent.xyz, boneWeights, boneIndices);
    }
    else
    {
        // [静的メッシュ・インスタンシングパス]
        // InstanceBuffer (t11) からワールド行列を取得して変形
        float4x4 world = GetInstanceWorld(instanceID);
        
        position = mul(position, world);
        vout.normal = normalize(mul(float4(normal, 0), world).xyz);
        vout.tangent.xyz = normalize(mul(float4(tangent.xyz, 0), world).xyz);
    }
    
	vout.vertex = mul(position, viewProjection);
	vout.texcoord = texcoord;
    vout.tangent.w = tangent.w;
	
    vout.position = position;
	
	// --- ライト視点の座標計算 ---
    vout.shadowPos = mul(position, light_view_projection);

    // インスタンスバッファから customParams を引っこ抜いて封筒に入れる
    vout.customParams = GetInstanceCustomParams(instanceID);
    
	return vout;
}
