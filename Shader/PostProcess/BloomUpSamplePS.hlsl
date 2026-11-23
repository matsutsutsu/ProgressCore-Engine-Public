// ===========================================================================
// 概要: ブルームの拡大。小さい段の光をぼかしながら広げ、1つ大きい段の光に足す
//
// 入力: 1つ小さい段の光（gInputTexture）・同じ大きさの縮小の段の光（gHighResTexture）
// 出力: 2つを足した光
//
// 備考: gTexelSize には C++ 側で radius が掛けてあり、大きいほど光が広くにじむ
// ===========================================================================
#include "Shader/PostProcess/Bloom.hlsli"

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float2 texel = gTexelSize;

    // 3x3 のテントフィルタ（中心 4・上下左右 2・斜め 1 の重みで、合計 16）
    float3 d = gInputTexture.Sample(LinearSampler, uv + float2(-1, -1) * texel).rgb * 1.0;
    float3 e = gInputTexture.Sample(LinearSampler, uv + float2(0, -1) * texel).rgb * 2.0;
    float3 f = gInputTexture.Sample(LinearSampler, uv + float2(1, -1) * texel).rgb * 1.0;

    float3 g = gInputTexture.Sample(LinearSampler, uv + float2(-1, 0) * texel).rgb * 2.0;
    float3 h = gInputTexture.Sample(LinearSampler, uv + float2(0, 0) * texel).rgb * 4.0;
    float3 i = gInputTexture.Sample(LinearSampler, uv + float2(1, 0) * texel).rgb * 2.0;

    float3 j = gInputTexture.Sample(LinearSampler, uv + float2(-1, 1) * texel).rgb * 1.0;
    float3 k = gInputTexture.Sample(LinearSampler, uv + float2(0, 1) * texel).rgb * 2.0;
    float3 l = gInputTexture.Sample(LinearSampler, uv + float2(1, 1) * texel).rgb * 1.0;

    float3 blur = (d + e + f + g + h + i + j + k + l) / 16.0f;

    float3 highRes = gHighResTexture.Sample(LinearSampler, uv).rgb;

    float3 result = blur + highRes;

    // NaN や無限大は 0 にする。残すと次の段で周りに広がり、黒い四角になる
    if (any(isnan(result)) || any(isinf(result)))
    {
        return float4(0, 0, 0, 0);
    }

    return float4(result, 1.0f);
}