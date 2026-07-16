// Shader/Material/Water/WaterShared.hlsli
//
// マテリアルの定数バッファ(SLOT_CB_MATERIAL, b0)は BindVS + BindPS の両方に
// バインドされている。VS/PS で同一の cbuffer 宣言を書けば、
// MaterialData の値を VS からも正しく読める。
// ズレを防ぐため共有ファイル化し、WaterVS / WaterPS の両方から include する。
#ifndef WATER_SHARED_HLSLI
#define WATER_SHARED_HLSLI

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 shallowColor;    // 浅瀬/正面から見た色
    float4 deepColor;       // 深部/斜めから見た色
    float  waveAmplitude;   // 波の高さ（頂点の上下動。0.05〜0.3目安）
    float  waveFrequency;   // 波の細かさ（0.5〜3目安）
    float  waveSpeed;       // 波の速さ（0.5〜2目安）
    float  sparkleIntensity;// キラキラの強さ（0〜1目安）
};

#endif // WATER_SHARED_HLSLI
