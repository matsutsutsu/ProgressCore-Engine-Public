// ===========================================================================
// 概要: エディタのレンダーターゲット表示（RenderTargetVisualizer）用。中間のバッファを見える色にする
//
// 入力: 表示するテクスチャ（2D か配列のどちらか）
// 出力: 画面に出せる 0〜1 の色
//
// 備考: そのままでは見えない値を、種類（gMode）ごとに直す
//         深度 … near / far で距離に直して白黒にする
//         法線 … ビュー空間の XY（-1〜1）を 0〜1 に直す
//         AO・配列 … R をそのまま白黒にする
//         HDR … 露出を掛けて Reinhard でトーンマップする
//       gChannelSelect で R / G / B / A の1つだけを白黒で見ることもできる
//       頂点シェーダーは FullScreenVS を使う
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/ShaderResources.h"

cbuffer CbDebug : register(REG_CB(SLOT_CB_RT_DEBUG))
{
    int   gMode;          // 表示の種類（DebugTexKind）。0=色 1=HDR 2=深度 3=1チャンネル 4=法線 5=配列
    float gExposure;      // HDR を表示するときの露出
    float gNearZ;         // 深度を距離に直すときの near
    float gFarZ;          // 深度を距離に直すときの far
    int   gArraySlice;    // 配列テクスチャの何枚目を表示するか
    int   gUseArray;      // 0=gTex を読む / 1=gTexArray を読む
    int   gChannelSelect; // 0=gMode のとおり 1=R 2=G 3=B 4=A（その値をそのまま白黒で）
    int   _pad0;
};

Texture2D      gTex      : register(REG_SRV(SLOT_SRV_RT_DEBUG));
Texture2DArray gTexArray : register(REG_SRV(SLOT_SRV_RT_DEBUG_ARRAY));
SamplerState   gSampler  : register(REG_SMP(SLOT_SMP_DEFAULT));

float4 main(VS_OUT input) : SV_TARGET
{
    float4 c = (gUseArray != 0)
        ? gTexArray.Sample(gSampler, float3(input.texcoord, (float)gArraySlice))
        : gTex.Sample(gSampler, input.texcoord);

    float3 outc;
    if (gMode == 2) // 深度: カメラからの距離に直し、far を白にする
    {
        float d = c.r;
        float denom = max(gFarZ - d * (gFarZ - gNearZ), 1e-6);
        float z = (gNearZ * gFarZ) / denom;
        outc = saturate(z / gFarZ).xxx;
    }
    else if (gMode == 3 || gMode == 5) // AO / 影の深度の配列: R をそのまま白黒に
    {
        outc = c.rrr;
    }
    else if (gMode == 4) // 法線: XY を 0〜1 に直す（B は 1 で埋める）
    {
        outc = float3(c.rg * 0.5 + 0.5, 1.0);
    }
    else if (gMode == 1) // HDR: 露出を掛けて Reinhard でトーンマップ
    {
        float3 h = c.rgb * gExposure;
        outc = h / (1.0 + h);
    }
    else // 色: そのまま
    {
        outc = c.rgb;
    }

    // 1チャンネルだけを見る指定があれば、gMode の結果より優先する
    if      (gChannelSelect == 1) outc = c.rrr;
    else if (gChannelSelect == 2) outc = c.ggg;
    else if (gChannelSelect == 3) outc = c.bbb;
    else if (gChannelSelect == 4) outc = c.aaa;

    return float4(outc, 1.0);
}
