#ifndef POST_PROCESS_VS_HLSLI
#define POST_PROCESS_VS_HLSLI

// FullScreenVS の出力と、ポストプロセスの PS の入力
// Common.hlsli の VS_OUT は 3D の描画用（法線などを持つ）で、FullScreenVS の出力と形が合わないので使わない
struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

#endif
