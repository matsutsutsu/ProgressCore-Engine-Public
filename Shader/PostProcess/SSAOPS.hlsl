// Shader/PostProcess/SSAOPS.hlsl
//
// SSAO(スクリーンスペース・アンビエントオクルージョン)。
// 深度バッファ(t4)だけから遮蔽を計算し、隙間・接地面・くぼみを暗くする。
//
// 【G-Buffer不要の理由】
//   法線バッファを持たないフォワード構成のため、近傍ピクセルの深度差から
//   ビュー空間法線を再構築する(ddx/ddyによる微分法)。これにより追加のRT無しで動く。
//
// 【割り切り(重要)】
//   本来AOは「環境光(IBL)にのみ」掛けるのが物理的に正しいが、ポストプロセスは
//   シェーディング済みの最終カラーしか受け取れないため、直接光や発光にも掛かる。
//   フォワード構成では一般的な妥協で、実用上の絵の改善効果は十分得られる。
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Shader/Common/DepthUtils.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneColor : register(REG_SRV(SLOT_SRV_BLOOM_IN));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

#define SSAO_SAMPLES 12

static const float3 kKernel[SSAO_SAMPLES] = {
    float3( 0.5381, 0.1856, 0.4319), float3( 0.1379, 0.2486, 0.4430),
    float3( 0.3371, 0.5679, 0.0057), float3(-0.6999,-0.0451, 0.0019),
    float3( 0.0689,-0.1598, 0.8547), float3( 0.0560, 0.0069, 0.1843),
    float3(-0.0146, 0.1402, 0.0762), float3( 0.0100,-0.1924, 0.0344),
    float3(-0.3577,-0.5301, 0.4358), float3(-0.3169, 0.1063, 0.0158),
    float3( 0.0103,-0.5869, 0.0046), float3(-0.0897,-0.4940, 0.3287)
};

float Rand(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;
    float3 scene = gSceneColor.Sample(LinearSampler, uv).rgb;

    float eyeDepth = SampleSceneEyeDepth(uv, LinearSampler);

    if (eyeDepth >= ssaoMaxDistance)
        return float4(scene, 1.0f);

    float3 viewPos = ReconstructViewPos(uv, eyeDepth);

    float3 normal = normalize(cross(ddx(viewPos), ddy(viewPos)));

    float  rot = Rand(uv) * 6.2831853f;
    float  cs = cos(rot), sn = sin(rot);

    float occlusion = 0.0f;
    [unroll]
    for (int i = 0; i < SSAO_SAMPLES; ++i)
    {
        float3 k = kKernel[i];
        float3 kr = float3(k.x * cs - k.y * sn, k.x * sn + k.y * cs, k.z);

        if (dot(kr, normal) < 0.0f) kr = -kr;

        float3 samplePos = viewPos + kr * ssaoRadius;

        float2 sampleUv = ViewPosToUV(samplePos);

        float sceneDepthAtSample = SampleSceneEyeDepth(sampleUv, LinearSampler);

        float diff = samplePos.z - sceneDepthAtSample;

        float rangeCheck = smoothstep(0.0f, 1.0f, ssaoRadius / max(abs(diff), 0.0001f));

        occlusion += (diff > ssaoBias) ? rangeCheck : 0.0f;
    }

    occlusion = 1.0f - (occlusion / (float)SSAO_SAMPLES) * ssaoIntensity;
    occlusion = saturate(occlusion);

    return float4(scene * occlusion, 1.0f);
}
