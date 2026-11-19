// ===========================================================================
// 概要: トレイルの歪みの層。加算の層と同じ三角形を、後ろの景色をずらした絵で塗る（屈折・陽炎）
//
// 入力: TrailVS の出力（頂点の色・ワールド座標）・シーンの色
// 出力: ずらした景色の色。α は尾のフェード（通常の半透明で重ねる）
//
// 備考: 頂点の色は、TrailSystem が歪みの層のために別の値を詰め直している
//         x = ずらす量（px）、y = 色のずれ（0〜1）、z = 陽炎の揺らぎ（0〜1）、w = 尾のフェード
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
    const float chroma = input.color.y;
    const float wobble = input.color.z;
    const float mask   = saturate(input.color.w);

    if (pixels <= 0.0f || mask <= 0.0f)
        discard;

    // ずらす向き。面の法線をビュー空間へ移した xy を使う
    // 法線はワールド座標の ddx / ddy から作るので、頂点に法線を持たせなくても、板・十字・筒のどれでも面の向きが取れる
    // 正規化しないのは、長さがそのまま「視線に対してどれだけ寝ているか」になるため（正面で 0、輪郭で最大）
    const float3 normalW = normalize(cross(ddx(input.worldPos), ddy(input.worldPos)));
    float2       dir     = mul(float4(normalW, 0.0f), gView).xy;

    // 板が正面を向くと法線からの歪みが消えるので、ノイズで向きを乱して陽炎にする
    if (wobble > 0.0f)
    {
        const float2 nuv = input.worldPos.xz * 0.05f + gTimeParams.x * float2(0.07f, -0.05f);
        const float2 n2  = float2(gGlobalNoiseMap.Sample(LinearSampler, nuv).r,
                                  gGlobalNoiseMap.Sample(LinearSampler, nuv + 0.37f).r);
        dir += (n2 - 0.5f) * 2.0f * wobble;
    }

    // px で指定したずらす量を UV に直す（gScreenParams.zw = 1 / 画面の幅, 1 / 画面の高さ）
    const float2 screenUV = input.position.xy * gScreenParams.zw;
    const float2 offset   = dir * pixels * mask * gScreenParams.zw;

    // 色のずれがあれば、赤と青を逆向きに少しずらして読む
    float3 refracted;
    if (chroma > 0.0f)
    {
        const float2 split = offset * chroma;
        refracted.r = gSceneColor.Sample(LinearSampler, saturate(screenUV + offset + split)).r;
        refracted.g = gSceneColor.Sample(LinearSampler, saturate(screenUV + offset)).g;
        refracted.b = gSceneColor.Sample(LinearSampler, saturate(screenUV + offset - split)).b;
    }
    else
    {
        refracted = gSceneColor.Sample(LinearSampler, saturate(screenUV + offset)).rgb;
    }

    return float4(refracted, mask);
}
