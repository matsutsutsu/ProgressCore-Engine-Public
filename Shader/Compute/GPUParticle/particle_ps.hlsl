// ===========================================================================
// 概要: GPU パーティクルのピクセルシェーダー。粒子の色にテクスチャを重ねる
//
// 入力: particle_gs の出力（色・UV・種類）
// 出力: 色（α 付き）。加算か半透明かは C++ のブレンドの設定で決まる
//
// 備考: 粒子のテクスチャは「黒い背景に白い絵、アルファは全面不透明」の白黒の画像を前提にする
//       そのため、アルファではなく明るさを不透明度として使う（黒い所が透明になる）
// ===========================================================================
#include "Shader/Compute/GPUParticle/particle.hlsli"

// サンプラーの種類の番号（今は使っていない）
#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2

SamplerState sampler_linear : register(REG_SMP(SLOT_SMP_LINEAR));

Texture2D color_map : register(REG_SRV(SLOT_SRV_MAT_TEX0)); // 煙・火花などの絵

float4 main(GS_OUT pin) : SV_TARGET
{
	float4 color = pin.color;

    // Point はテクスチャを使わず、粒子の色だけで塗る
    if (pin.type == 0){ return color; }

    // UV スクロール
    // スプライトシートのときはずらさない。UV は GS でコマの矩形の中に収めてあるので、
    // ずらすとコマの外へはみ出して隣の絵がにじむ（インスペクタでも、スプライトシートのときはスクロールを入力できなくしてある）
    float2 uv = pin.texcoord;

    if (sprite_sheet_grid.x * sprite_sheet_grid.y <= 1)
        uv += uv_scroll_speed * time;

    float4 texColor = color_map.Sample(sampler_linear, uv);

	// 明るさ（Rec.601 の輝度の係数）
    float brightness = dot(texColor.rgb, float3(0.299, 0.587, 0.114));

    // 不透明度 = テクスチャのアルファ × 明るさ
    // 色       = テクスチャの色味だけ（明るさで割って 1 にそろえる。明るさは不透明度の方で効かせる）
    // 色にも明るさを掛けると、加算では明るさが3回掛かり（色・明るさ・不透明度）、縁が沈んで粒子全体が薄く見える
    // 白黒の絵なら色味は 1 で、粒子の色がそのまま出る
    color.a   *= texColor.a * brightness;
    color.rgb *= texColor.rgb / max(brightness, 1e-3f);

	return color;
}
