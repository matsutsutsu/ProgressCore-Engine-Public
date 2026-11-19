// ===========================================================================
// 概要: トレイルの加算の層。頂点の色にトレイルのテクスチャを掛ける
//
// 入力: TrailVS の出力（頂点の色・UV）
// 出力: 色（加算ブレンド SrcAlpha, One で重ねる）
//
// 備考: テクスチャを設定していないトレイルには、C++ が 1x1 の白を置く。白 × 色 = 色なので、単色のトレイルになる
//       テクスチャの α で輪郭の形、RGB で模様、U（長さの方向）のグラデーションで寿命に沿った色の変化を出せる
//       rgb に α を掛けておかない（ブレンドの SrcAlpha が掛ける）
// ===========================================================================
#include "Shader/Common/Scene.hlsli"

Texture2D    gTrailTex : register(REG_SRV(SLOT_SRV_TRAIL_TEX));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float3 worldPos : TEXCOORD0;
    float2 uv       : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET
{
    float4 tex = gTrailTex.Sample(LinearSampler, input.uv);
    return input.color * tex;
}
