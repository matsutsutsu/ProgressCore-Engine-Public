// ===========================================================================
// 概要: Basic マテリアル。ライトを当てず、テクスチャの色に baseColor を掛けるだけ
//
// 入力: BasicVS の出力（UV だけ使う）
// 出力: 色（α 付き）
// ===========================================================================
#include "Shader/Common/Common.hlsli"

cbuffer CbMesh : register(REG_CB(SLOT_CB_MESH))
{
	float4		baseColor; // テクスチャに掛ける色
};

Texture2D AlbedoMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
	return AlbedoMap.Sample(LinearSampler, pin.texcoord) * baseColor;
}
