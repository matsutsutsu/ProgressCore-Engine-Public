// ===========================================================================
// 概要: SSAO の1段目。各ピクセルが周りの物にどれだけ遮られているかを求める
//
// 入力: シーンの深度・ビュー空間の法線
// 出力: 明るさに掛ける値（0〜1。1 で遮られていない）を R8 に書く
//
// 備考: ピクセルごとにサンプルの向きを回すので、ノイズが出る。消すのは次の段のぼかし
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Shader/Common/DepthUtils.hlsli"
#include "Shader/PostProcess/SSAO.hlsli"

SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

#define SSAO_SAMPLES 12

// 調べる点の並び（z が正の半球の中）。ピクセルごとに z 軸まわりに回し、法線の側へ折り返して使う
static const float3 kKernel[SSAO_SAMPLES] = {
    float3( 0.5381, 0.1856, 0.4319), float3( 0.1379, 0.2486, 0.4430),
    float3( 0.3371, 0.5679, 0.0057), float3(-0.6999,-0.0451, 0.0019),
    float3( 0.0689,-0.1598, 0.8547), float3( 0.0560, 0.0069, 0.1843),
    float3(-0.0146, 0.1402, 0.0762), float3( 0.0100,-0.1924, 0.0344),
    float3(-0.3577,-0.5301, 0.4358), float3(-0.3169, 0.1063, 0.0158),
    float3( 0.0103,-0.5869, 0.0046), float3(-0.0897,-0.4940, 0.3287)
};

// uv から決まる 0〜1 の疑似乱数
float Rand(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float main(VS_OUT input) : SV_TARGET
{
    // 処理フロー:
    // 1. このピクセルのビュー空間の位置と法線を求める
    // 2. 周りの点を調べ、物の奥に入った点を数える
    // 3. 数えた割合を、明るさに掛ける値にする

    // ---------------------------------------------------------
    // 1. 位置と法線
    // ---------------------------------------------------------

    float2 uv = input.texcoord;

    // ssaoMaxDistance より遠い所は、遮られていない扱いにして計算しない
    float eyeDepth = SampleSceneEyeDepth(uv, LinearSampler);
    if (eyeDepth >= ssaoMaxDistance) return 1.0f;

    float3 viewPos = ReconstructViewPos(uv, eyeDepth);
    float3 normal  = SampleSceneViewNormal(uv, LinearSampler);

    // 調べる点を回す角度。隣のピクセルと向きを変え、少ないサンプルの偏りをノイズに散らす
    float rot = Rand(uv) * 6.2831853f;
    float cs = cos(rot), sn = sin(rot);

    // ---------------------------------------------------------
    // 2. 周りの点を調べる
    // ---------------------------------------------------------

    float occlusion = 0.0f;
    [unroll]
    for (int i = 0; i < SSAO_SAMPLES; ++i)
    {
        // 表の点を回し、面の裏側に出た点は法線の側へ折り返す
        float3 k  = kKernel[i];
        float3 kr = float3(k.x * cs - k.y * sn, k.x * sn + k.y * cs, k.z);
        if (dot(kr, normal) < 0.0f) kr = -kr;

        // 調べる点を画面に投影し、そこに見えている物の深度と比べる
        float3 samplePos          = viewPos + kr * ssaoRadius;
        float2 sampleUv           = ViewPosToUV(samplePos);
        float  sceneDepthAtSample = SampleSceneEyeDepth(sampleUv, LinearSampler);
        float  diff               = samplePos.z - sceneDepthAtSample;

        // 点が物の奥にあれば、遮られていると数える
        // 深度の差が半径より大きいもの（離れた別の物）は弱めて数える
        float rangeCheck = smoothstep(0.0f, 1.0f, ssaoRadius / max(abs(diff), 0.0001f));
        occlusion += (diff > ssaoBias) ? rangeCheck : 0.0f;
    }

    // ---------------------------------------------------------
    // 3. 明るさに掛ける値
    // ---------------------------------------------------------

    // ssaoIntensity が 1 未満なら、遮られた分を薄めて返す
    float occ = occlusion / (float)SSAO_SAMPLES;
    float ao  = saturate(1.0f - occ);
    return lerp(1.0f, ao, saturate(ssaoIntensity));
}
