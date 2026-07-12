// Shader/PostProcess/SSAOPS.hlsl
// SSAO 第1パス: 遮蔽値(0..1)のみを R8 へ出力。シーン色は扱わない。
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Shader/Common/DepthUtils.hlsli"
#include "Shader/PostProcess/SSAO.hlsli"

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

float main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;

    float eyeDepth = SampleSceneEyeDepth(uv, LinearSampler);

    if (eyeDepth >= ssaoMaxDistance)
        return 1.0f;

    float3 viewPos = ReconstructViewPos(uv, eyeDepth);
    float3 normal = SampleSceneViewNormal(uv, LinearSampler);

    float rot = Rand(uv) * 6.2831853f;
    float cs = cos(rot), sn = sin(rot);

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

    float occ = occlusion / (float)SSAO_SAMPLES;
    float ao  = saturate(1.0f - occ);
    return lerp(1.0f, ao, saturate(ssaoIntensity));
}
