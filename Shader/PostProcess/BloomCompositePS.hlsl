// ===========================================================================
// 概要: ブルームの最後の段。元のシーンに、拡大し終えた光を足す
//
// 入力: 元のシーンの色（gInputTexture）・拡大し終えた光（gHighResTexture）
// 出力: 光を足したシーンの色（HDR）
// ===========================================================================
#include "Shader/PostProcess/Bloom.hlsli"

float4 main(VS_OUT input) : SV_TARGET
{
    float3 scene = gInputTexture.Sample(LinearSampler, input.texcoord).rgb;
    float3 glow = gHighResTexture.Sample(LinearSampler, input.texcoord).rgb;

    float3 result = scene + glow;

    // NaN や無限大は黒にする。残すと後ろのエフェクトのぼかしで周りに広がる
    if (any(isnan(result)) || any(isinf(result)))
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return float4(result, 1.0f);
}