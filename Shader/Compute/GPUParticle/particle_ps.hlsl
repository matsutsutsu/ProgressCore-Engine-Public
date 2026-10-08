#include "Shader/Compute/GPUParticle/particle.hlsli"

//----------------------------------------------
// サンプラーの種類を定義
//----------------------------------------------
// POINT        : 最近傍サンプリング（1ピクセルをそのまま）
// LINEAR       : 線形補間（滑らか）
// ANISOTROPIC  : 異方性フィルタリング（角度に応じて補間）
#define POINT 0
#define LINEAR 1
#define ANISOTROPIC 2

// サンプラー配列（Texture sampling の設定）
//SamplerState sampler_states[3] : register(REG_SMP_LINEAR);
SamplerState sampler_linear : register(REG_SMP(SLOT_SMP_LINEAR));

// パーティクルのテクスチャ（例：煙・火花などの画像）
Texture2D color_map : register(REG_SRV(SLOT_SRV_MAT_TEX0));

//----------------------------------------------
// ピクセルごとに実行されるシェーダー
//----------------------------------------------
float4 main(GS_OUT pin) : SV_TARGET
{
	// -----------------------------
	// ジオメトリシェーダーから受け取った基本色
	// （例えばパーティクルの発光色など）
	// -----------------------------
	float4 color = pin.color;
    
    // pin.type は 0 = Point, 1 = Quad (GSでセットされている)
    // **Point の場合**：テクスチャは使わず color を返す（完全に自前色）
    if (pin.type == 0){ return color; }

    // ---------------------------------------------------------
    // UVスクロール
    // ---------------------------------------------------------
    float2 uv = pin.texcoord;

    // スプライトシート使用時はスクロールしない。
    //   pin.texcoord は GS で「コマの矩形内」に収めた値なので、ここでズラすと
    //   コマの外へはみ出して隣の絵が滲む（無言で壊れる組み合わせだった）。
    //   併用したい場合はコマ内で折り返す実装が必要。インスペクタ側でも
    //   スプライトシート使用中は UV スクロールの入力を無効化してある。
    if (sprite_sheet_grid.x * sprite_sheet_grid.y <= 1)
        uv += uv_scroll_speed * time;   // 時間経過(time) × 速度(speed) を足してズラす

	//------------------------------------------
    // 【テクスチャを明るさマスクとして使う】
    // 粒子のテクスチャは「黒背景に白い絵、アルファは全面不透明」の白黒画像なので、
    // アルファではなく明るさを不透明度として使う（黒い部分が透明になる）。
    //
    // 明るさは不透明度にだけ掛け、色には掛けない。以前は色にも掛けていたので、
    // 加算では 色 × 明るさ × 明るさ × 不透明度(明るさ) と3回掛かり、
    // 明るさ 0.5 の縁が 0.125 まで沈んで粒子全体が薄く見えていた。
    //------------------------------------------
    float4 texColor = color_map.Sample(sampler_linear, uv);

	// 輝度計算：人間の目の感度に基づいた重み付き平均
    float brightness = dot(texColor.rgb, float3(0.299, 0.587, 0.114));

    // 描画モード（加算/半透明）の切り替えは C++ 側の PSO (BlendState) が担う。
    //   不透明度 = テクスチャのアルファ × 明るさ
    //   色       = テクスチャの色味だけ（明るさで割って 1 にそろえる。明るさはマスクが担う）
    //   白黒の絵なら色味は 1 で color がそのまま出る。色付きの絵でも、加算の結果は
    //   color × テクスチャの色 × 不透明度 となり、絵の見た目どおりになる。
    color.a   *= texColor.a * brightness;
    color.rgb *= texColor.rgb / max(brightness, 1e-3f);

	return color;
}
