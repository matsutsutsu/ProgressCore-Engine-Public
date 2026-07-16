// Shader/Material/Grass/GrassVS.hlsl
//
// 両面カリング(cull:none)+ アルファクリップの草マテリアル用の頂点シェーダー。
// 現時点では風揺れ等の頂点アニメは行わない、素の座標変換のみ。
//
// ★将来の拡張余地(今回は未実装):
//   1) 静的なバリエーション: customParams.x（インスタンスごとに自動で渡ってくる
//      汎用チャンネル、MaterialComponent::customParams 参照）を使えば、
//      C++/エンジン側の変更なしに「同じ草メッシュでも1本1本傾きが違う」
//      という静的な自然さは HLSL だけで追加できる。
//   2) 時間経過での揺れ(アニメーション): これは CbScene(マテリアル用, b8)に
//      現状 経過時間(gTime)が乗っていないため、HLSL だけでは実現できない
//      （エンジン側にVSリフレクション対応か時間cbufferの追加が必要）。
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

    float3 worldTangent;
    CalculateWorldTransform(
        position, normal, tangent,
        boneWeights, boneIndices, instanceID,
        vout.position, vout.normal, worldTangent
    );
    vout.tangent = float4(worldTangent, tangent.w);

    vout.vertex = mul(vout.position, viewProjection);
    vout.texcoord = texcoord;
    vout.customParams = GetInstanceCustomParams(instanceID);

    return vout;
}
