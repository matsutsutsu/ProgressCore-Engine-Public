#ifndef BLOOM_HLSLI
#define BLOOM_HLSLI

// ===========================================================================
// 概要: ブルームの5つのシェーダー（抽出・縮小・拡大・合成）と FullScreenVS が共有する宣言
//
// 備考: cbuffer は PostProcessShared.h の CbBloomData と同じ並び。並びを変えるときは両方を直す
// ===========================================================================
#include "Engine/Graphics/Shader/ShaderResources.h"

// 中身は PostProcessVS.hlsli の VS_OUT と同じ。FullScreenVS からどの PS にもつながる
struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

cbuffer CbBloom : register(REG_CB(SLOT_CB_BLOOM))
{
    float gThreshold; // 光らせ始める明るさ（既定 1.0）
    float gSoftKnee;  // しきい値の前後をなめらかにつなぐ幅。gThreshold に掛ける割合（既定 0.5）
    float gIntensity; // 抜き出した光に掛ける強さ（既定 1.0）
    float gPadding;   // C++ では radius（拡大の間隔に掛ける値。gTexelSize に掛けて渡すので、ここでは読まない）

    float2 gTexelSize; // 読むテクスチャの1ピクセルの大きさ（UV）。拡大の段では radius を掛けてある
    float2 gPadding2;
};

Texture2D gInputTexture   : register(REG_SRV(SLOT_SRV_POST_INPUT0));
Texture2D gHighResTexture : register(REG_SRV(SLOT_SRV_POST_INPUT1)); // 拡大と合成で足す、1つ大きい段の画像

SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));


#endif