// ===========================================================================
// 概要: トーンマップ。HDR の色を、モニターに出せる 0〜1 の色に直す
//
// 入力: すべてのエフェクトをかけ終えたシーンの色（HDR）
// 出力: ガンマ補正まで済ませた 0〜1 の色
//
// 備考: エフェクトの列の後に必ず1回走る
//       バックバッファは sRGB ではない UNORM なので、ガンマ補正はここで行う
// ===========================================================================
#include "Shader/PostProcess/Bloom.hlsli"

// PostProcessShared.h の CbToneMapData と同じ並び
cbuffer CbToneMap : register(REG_CB(SLOT_CB_TONEMAP)) {
    float gExposure;            // 露出。トーンマップの前に色へ掛ける
    float gChromaticAberration; // 赤と青をずらす量。0 でずらさない（色収差のエフェクトとは別）
    float2 ggPadding;
    float4 gColorTint;          // rgb = 色へ掛ける色味
};

// ACES の近似式（Narkowicz）
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;

    // 画面の中心から外へ向かう向き。縁ほど大きい
    float2 centerDir = uv - 0.5f;

    // 赤を内へ、青を外へずらして読む。緑は基準なのでずらさない
    float2 uvR = uv - centerDir * gChromaticAberration;
    float2 uvG = uv;
    float2 uvB = uv + centerDir * gChromaticAberration;

    float r = gInputTexture.Sample(LinearSampler, uvR).r;
    float g = gInputTexture.Sample(LinearSampler, uvG).g;
    float b = gInputTexture.Sample(LinearSampler, uvB).b;

    float3 hdrColor = float3(r, g, b);

    hdrColor *= gExposure * gColorTint.rgb;

    float3 sdrColor = ACESFilm(hdrColor);

    // ガンマ補正（1/2.2 乗）
    sdrColor = pow(sdrColor, float3(0.4545f, 0.4545f, 0.4545f));

    return float4(sdrColor, 1.0f);
}