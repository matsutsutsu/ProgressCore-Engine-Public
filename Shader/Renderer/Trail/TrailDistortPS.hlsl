// ===========================================================================
// 概要: トレイルの歪みの層。加算の層と同じ三角形を、後ろの景色をずらした絵で塗る（屈折）
//
// 入力: TrailVS の出力（頂点の色・ワールド座標）・シーンの色
// 出力: ずらした景色の色。α は尾のフェード（通常の半透明で重ねる）
//
// 備考: 頂点の色は、TrailSystem が歪みの層のために別の値を詰め直している
//         x = ずらす量（px）、w = 尾のフェード（y, z は使わない）
//       シーンの色は SceneColorGrabPass が先に取ってあるので、レンダーターゲットもコピーも増やさずに済む
// ===========================================================================
#include "Shader/Common/Scene.hlsli"

Texture2D    gSceneColor   : register(REG_SRV(SLOT_SRV_SCENE_COLOR));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float3 worldPos : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    const float pixels = input.color.x;
    const float mask   = saturate(input.color.w);

    if (pixels <= 0.0f || mask <= 0.0f)
        discard;

    // ずらす向き。面の法線をビュー空間へ移した xy を使う
    // 法線はワールド座標の ddx / ddy から作るので、頂点に法線を持たせなくても、板・筒のどちらでも面の向きが取れる
    // 正規化しないのは、長さがそのまま「視線に対してどれだけ寝ているか」になるため（正面で 0、輪郭で最大）
    const float3 normalW = normalize(cross(ddx(input.worldPos), ddy(input.worldPos)));
    const float2 dir     = mul(float4(normalW, 0.0f), gView).xy;

    // px で指定したずらす量を UV に直す（gScreenParams.zw = 1 / 画面の幅, 1 / 画面の高さ）
    const float2 screenUV = input.position.xy * gScreenParams.zw;
    const float2 offset   = dir * pixels * mask * gScreenParams.zw;

    const float3 refracted = gSceneColor.Sample(LinearSampler, saturate(screenUV + offset)).rgb;

    return float4(refracted, mask);
}
