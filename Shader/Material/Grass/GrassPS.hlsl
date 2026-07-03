// Shader/Material/Grass/GrassPS.hlsl
//
// 両面カリング(cull:none)の草・葉マテリアル用のピクセルシェーダー。
// テクスチャのアルファでシルエットを切り抜き(clip)、
// 板ポリの裏側も表側と同じ見た目で描画される（PSOのcull:none設定による）。
//
// テクスチャスロット名は既存エディタ(MaterialComponentMeta.cpp)の
// "Textures"パネルが持つ slotNames と揃えてある("DiffuseMap")ため、
// エディタ側の追加変更なしにそのままテクスチャを割り当てられる。
#include "Shader/Common/Common.hlsli"

// ==============================================================================
// 定数バッファ
//   ★ 変数名がそのまま MaterialData の nameHash キーになる
// ==============================================================================
cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 baseColor;          // 全体の色調（テクスチャに掛け合わせる。既定は白のまま=テクスチャそのまま）
    float  alphaClipThreshold; // このアルファ値未満は描画しない（0.5前後が標準的な切り抜き閾値）
    float3 _pad;               // 16byte境界合わせ
};

Texture2D DiffuseMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 tex = DiffuseMap.Sample(LinearSampler, pin.texcoord);

    // アルファが閾値未満のピクセルは破棄する（葉っぱの形に切り抜く）。
    // clip()はピクセルシェーダー内でSV_TARGETを一切書かずに終了させる命令。
    clip(tex.a - alphaClipThreshold);

    // Unlit: ライティング計算は行わず、テクスチャ×基本色をそのまま出力。
    // (PBR等と同じライティングにしたい場合は PBR_PS.hlsl の陰影計算部分を
    //  ここに移植すればよいが、草は動きが多く軽量描画が優先されるため
    //  まずはUnlitを既定とする)
    return float4(tex.rgb * baseColor.rgb, 1.0f);
}
