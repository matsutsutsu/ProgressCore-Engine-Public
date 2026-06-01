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
    
    // 時間経過(time) × 速度(speed) を足してズラす
    uv += uv_scroll_speed * time;    

	//------------------------------------------
    // 【テクスチャの明度を掛ける処理】
    // color_map（煙などのテクスチャ）をサンプリングして、
    // RGBの明るさを計算（グレースケール化）して
    // 元の color に掛ける。
    //
    // 明度が低い部分は透明になる！
    // → 白黒画像を明るさマスクとして使う感じ。
    // → 火花や煙などの「透明部分」を制御する用途。
    //------------------------------------------
    float4 texColor = color_map.Sample(sampler_linear, uv);
    
    // ★ 修正4 & 6: render_mode分岐と #if 0 クルフトを全削除！
    // 描画モード（加算/半透明）の切り替えはC++側のPSO (BlendState) が行います。
    // シェーダーは純粋にテクスチャの色とアルファを掛け合わせるだけに徹します。
    color.rgb *= texColor.rgb;
    color.a *= texColor.a;

	// 輝度計算：人間の目の感度に基づいた重み付き平均
    float brightness = dot(texColor.rgb, float3(0.299, 0.587, 0.114));

	// 元の色に明るさを掛けて暗く／明るくする
    color *= brightness;

	return color;
}
