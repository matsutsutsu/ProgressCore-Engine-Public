// ===========================================================================
// 概要: スプライト（2D の画像）。テクスチャの色に頂点の色を掛ける
//
// 入力: SpriteVS の出力（頂点の色・UV）
// 出力: 色（α 付き）
// ===========================================================================
#include "Shader/Material/Sprite/Sprite.hlsli"
#include "Engine/Graphics/Shader/ShaderResources.h"

Texture2D spriteTexture : register(REG_SRV(SLOT_SRV_SPRITE));
SamplerState spriteSampler : register(REG_SMP(SLOT_SMP_DEFAULT));

float4 main(VS_OUT pin) : SV_TARGET
{
	return spriteTexture.Sample(spriteSampler, pin.texcoord) * pin.color;
}
