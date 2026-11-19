// ===========================================================================
// 概要: ブルームの1段目。シーンから、しきい値より明るい所の光だけを抜き出す
//
// 入力: シーンの色（HDR）
// 出力: 抜き出した光。出力先は入力の半分の大きさなので、読むときに縮小も兼ねる
//
// 備考: しきい値はきっぱり切らず、gSoftKnee の幅でなめらかにつなぐ（ソフトニー）
// ===========================================================================
#include "Shader/PostProcess/Bloom.hlsli"

float4 main(VS_OUT input) : SV_TARGET
{
    float4 color = gInputTexture.Sample(LinearSampler, input.texcoord);

    // 負の色は 0 にする。そのまま後ろの段へ流すと、割り算や累乗で NaN になる
    color.rgb = max(0.0f, color.rgb);

    // 明るさ（Rec.709 の輝度の係数）
    float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));

    // しきい値の手前 knee の幅だけ、0 から少しずつ光らせる
    float knee = gThreshold * gSoftKnee;
    float soft = brightness - gThreshold + knee;
    soft = clamp(soft, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + 0.00001f);

    // 色に掛ける割合。明るさのうち、光として残す分
    float contribution = max(soft, brightness - gThreshold);
    contribution /= max(brightness, 0.00001f);

    return color * contribution * gIntensity;
}