// Shader/Renderer/Trail/TrailDistortPS.hlsl
// トレイルの「歪み(屈折)レイヤ」。加算レイヤと全く同じ三角形を、
// 通常αブレンドで背景の屈折像に置き換えて描く。
//
// 実装の意図(Why):
//   - SceneColorGrabPass が TrailPass より前に不透明＋スカイまでのシーンカラーを
//     t5 へ確保済み。追加の RT もコピーも要らないので屈折が安く成立する。
//   - ずらし方向は面の法線をビュー空間へ落とした xy。正規化しないのは、
//     長さがそのまま「視線に対してどれだけ寝ているか」になり、面が正面を
//     向くと 0、輪郭で最大という屈折らしい減衰が自然に得られるため。
//   - 法線は worldPos の ddx/ddy から作る。頂点に法線を持たせずに済み、
//     板・十字・筒のどれでも実際の面の向きが取れる。
//
// 頂点カラーの意味（TrailSystem が歪みレイヤ用に詰め替えている）:
//   x = 歪み量(px) / y = 色ズレ(0〜1) / z = 陽炎ゆらぎ(0〜1) / w = 尾のフェード
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

    const float3 normalW = normalize(cross(ddx(input.worldPos), ddy(input.worldPos)));
    float2       dir     = mul(float4(normalW, 0.0f), gView).xy;

    // 板が正面を向くと法線由来の歪みが消えるため、ノイズで方向を乱して陽炎にする。
    if (wobble > 0.0f)
    {
        const float2 nuv = input.worldPos.xz * 0.05f + gTimeParams.x * float2(0.07f, -0.05f);
        const float2 n2  = float2(gGlobalNoiseMap.Sample(LinearSampler, nuv).r,
                                  gGlobalNoiseMap.Sample(LinearSampler, nuv + 0.37f).r);
        dir += (n2 - 0.5f) * 2.0f * wobble;
    }

    // gScreenParams.zw = 1/画面幅, 1/画面高。px 指定を UV 量へ直す。
    const float2 screenUV = input.position.xy * gScreenParams.zw;
    const float2 offset   = dir * pixels * mask * gScreenParams.zw;

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
