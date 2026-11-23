// ===========================================================================
// 概要: スプライトと UI のゲージの頂点シェーダー。頂点をそのまま渡す
//
// 入力: 画面の位置（C++ がクリップ空間に直して渡す）・色・UV
// 出力: 入力と同じ
// ===========================================================================
#include "Shader/Material/Sprite/Sprite.hlsli"

VS_OUT main(float4 position : POSITION, float4 color : COLOR, float2 texcoord : TEXCOORD)
{
	VS_OUT vout;
	vout.position = position;
	vout.color = color;
	vout.texcoord = texcoord;

	return vout;
}
