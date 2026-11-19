// ===========================================================================
// 概要: 線（ShapeRenderer）の頂点シェーダー。元の線分をインスタンスの行列で伸ばして置く
//
// 入力: 線分の頂点（位置）とインスタンスの番号
// 出力: 画面の位置・色（customParams）
// ===========================================================================
#include "Shader/Renderer/Shape/ShapeRenderer.hlsli"

VS_OUT main(float4 position : POSITION, uint instanceID : SV_InstanceID)
{
    VS_OUT vout;

    float4x4 world = GetInstanceWorld(instanceID);
    float4 instanceColor = GetInstanceCustomParams(instanceID);

    float4 worldPos = mul(position, world);

    vout.position = mul(worldPos, viewProjection);
    vout.color = instanceColor;

    return vout;
}