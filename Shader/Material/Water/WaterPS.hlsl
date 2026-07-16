// Shader/Material/Water/WaterPS.hlsl
// Category : unlit(簡易フレネル)
// PSO      : blend=transparency, depth=testOnly
//            (shaders.json: { "name":"Water", "blend":"transparency", "depth":"testOnly" })
// Params   : WaterShared.hlsli 参照
// Textures : なし（gGlobalNoiseMap を自動使用）
// Use      : 池・川・水たまりの波打つ水面。平面メッシュに適用。
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"
#include "WaterShared.hlsli"

SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 normal  = normalize(pin.normal);
    float3 viewDir = normalize(cameraPosition.xyz - pin.position.xyz);
    float  NdotV   = saturate(dot(normal, viewDir));

    float3 baseColor = lerp(deepColor.rgb, shallowColor.rgb, NdotV);
    float  baseAlpha = lerp(deepColor.a, shallowColor.a, NdotV);

    float2 sparkleUV = pin.position.xz * 0.15f;
    float  n = SampleGlobalNoise(LinearSampler, sparkleUV, float2(0.03f, 0.02f) * waveSpeed);
    float  sparkle = pow(saturate(n), 8.0f) * sparkleIntensity;

    float3 finalColor = baseColor + sparkle;

    return float4(finalColor, baseAlpha);
}
