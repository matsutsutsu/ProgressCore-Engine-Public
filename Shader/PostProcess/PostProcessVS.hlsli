#ifndef POST_PROCESS_VS_HLSLI
#define POST_PROCESS_VS_HLSLI

// FullScreenVS と PS の入力シグネチャを一致させる最小構造体。
// Common.hlsli の VS_OUT は 3D 描画用(NORMAL/TANGENT 等)なのでポストプロセスでは使わない。
struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

#endif
