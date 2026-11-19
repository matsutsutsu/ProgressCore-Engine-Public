// ===========================================================================
// 概要: 線（ShapeRenderer）のピクセルシェーダー。インスタンスの色で塗るだけ
//
// 入力: ShapeRendererVS の出力（色）
// 出力: 色
// ===========================================================================
#include "Shader/Renderer/Shape/ShapeRenderer.hlsli"

float4 main(VS_OUT pin) : SV_TARGET
{
    return pin.color;
}