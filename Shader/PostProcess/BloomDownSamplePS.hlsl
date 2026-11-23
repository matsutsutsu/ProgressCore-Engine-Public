// ===========================================================================
// 概要: ブルームの縮小。1つ上の段の光を、半分の大きさにしながらぼかす
//
// 入力: 1つ上の段の光（gTexelSize はこの入力の1ピクセルの大きさ）
// 出力: 半分の大きさの光
//
// 備考: 点の位置は一般的な 13 タップの縮小と同じだが、重みは違う
//       重みの合計が 1 にならず（約 0.78）、縮小するたびに少し暗くなる
// ===========================================================================
#include "Shader/PostProcess/Bloom.hlsli"

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv    = input.texcoord;
    float2 texel = gTexelSize;

    // --- 13 点を取る ---

    // 中心
    float3 c_center = gInputTexture.Sample(LinearSampler, uv).rgb;

    // 内側の4点（斜めに1ピクセル）
    float3 c_inner = (
        gInputTexture.Sample(LinearSampler, uv + float2(-1, -1) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(1, -1) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(-1, 1) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(1, 1) * texel).rgb
    );

    // 外側の8点（2ピクセル離れた四隅と上下左右）
    float3 c_outer = (
        gInputTexture.Sample(LinearSampler, uv + float2(-2, -2) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(0, -2) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(2, -2) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(-2, 0) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(2, 0) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(-2, 2) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(0, 2) * texel).rgb +
        gInputTexture.Sample(LinearSampler, uv + float2(2, 2) * texel).rgb
    );

    // --- 重みを付けて平均する ---

    // 内側を重くした平均（重みの合計はヘッダの備考のとおり 1 にならない）
    float3 color = 0.0f;
    color += c_inner * 0.5f;
    color += (c_outer + c_center) * 0.125f;
    color /= 4.0f;

    // NaN や無限大は 0 にする。残すと次の段で周りに広がり、黒い四角になる
    if (any(isnan(color)) || any(isinf(color))) return float4(0, 0, 0, 0);

    return float4(color, 1.0f);
}
