// Shader/PostProcess/RenderTargetDebugPS.hlsl
// RenderDebugWindow(v2) 用の可視化ピクセルシェーダー。
// 中間バッファの種別(gMode)に応じて「そのままでは読めない値」を表示可能な色へ remap する。
//   - Depth  : 透視深度を near/far で線形化してグレースケール
//   - Normal : ビュー空間法線 XY(RG) を [-1,1]→[0,1] へ
//   - AO/配列: R 成分をグレースケール（配列はスライス選択）
//   - HDR    : 露出を掛けて Reinhard トーンマップ
// さらにチャンネル単離(gChannelSelect)で R/G/B/A を個別グレースケール表示できる。
// VS は FullScreenVS.cso を共用（SV_VertexID で全画面三角形）。
#include "Shader/PostProcess/PostProcessVS.hlsli"

cbuffer CbDebug : register(b0)
{
    int   gMode;          // DebugTexKind: 0=Color 1=HDRColor 2=Depth 3=SingleChannel 4=Normal 5=Array
    float gExposure;      // HDR 表示用の露出
    float gNearZ;         // 深度線形化用
    float gFarZ;
    int   gArraySlice;    // 配列テクスチャの表示スライス
    int   gUseArray;      // 0=Texture2D(t0) / 1=Texture2DArray(t1)
    int   gChannelSelect; // 0=RGB(mode準拠) 1=R 2=G 3=B 4=A（生値をグレースケール）
    int   _pad0;
};

Texture2D      gTex      : register(t0);
Texture2DArray gTexArray : register(t1);
SamplerState   gSampler  : register(s0);

float4 main(VS_OUT input) : SV_TARGET
{
    // 種別に応じてソースをサンプル（配列はスライス指定）
    float4 c = (gUseArray != 0)
        ? gTexArray.Sample(gSampler, float3(input.texcoord, (float)gArraySlice))
        : gTex.Sample(gSampler, input.texcoord);

    float3 outc;
    if (gMode == 2) // Depth: 透視深度を線形化
    {
        float d = c.r;
        float denom = max(gFarZ - d * (gFarZ - gNearZ), 1e-6);
        float z = (gNearZ * gFarZ) / denom;
        outc = saturate(z / gFarZ).xxx;
    }
    else if (gMode == 3 || gMode == 5) // AO(R) / 配列シャドウ深度: R をそのままグレースケール
    {
        outc = c.rrr;
    }
    else if (gMode == 4) // Normal: ビュー空間 XY(RG) を色域へ
    {
        outc = float3(c.rg * 0.5 + 0.5, 1.0);
    }
    else if (gMode == 1) // HDR: 露出 + Reinhard トーンマップ
    {
        float3 h = c.rgb * gExposure;
        outc = h / (1.0 + h);
    }
    else // Color: そのまま
    {
        outc = c.rgb;
    }

    // チャンネル単離（生値をグレースケール表示）
    if      (gChannelSelect == 1) outc = c.rrr;
    else if (gChannelSelect == 2) outc = c.ggg;
    else if (gChannelSelect == 3) outc = c.bbb;
    else if (gChannelSelect == 4) outc = c.aaa;

    return float4(outc, 1.0);
}
