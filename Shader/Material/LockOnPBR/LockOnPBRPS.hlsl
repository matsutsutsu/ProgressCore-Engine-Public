// Shader/Material/LockOnPBR/LockOnPBRPS.hlsl
// Category : lit
// PSO      : 既定(不透明)  ※本体を光らせるだけなので通常の不透明キューでよい
// Params   : PBRと同じ一式 + lockOnIntensity / pulseSpeed / rimColor / rimPower
// Textures : Albedo(t0)/Normal(t1)/MetalRough(t2)/Emissive(t3)  ※PBRと同じ
// Use      : ロックオン中の敵。lockOnIntensity をゲーム側から 0→1 にすると輪郭が光る。
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/PBRLighting.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 baseColor;
    float3 emissiveColor;
    float  metalness;
    float  roughness;
    float  occlusionStrength;
    float  useAlbedoMap;
    float  useNormalMap;
    float  useMetalRoughMap;
    float  useOcclusionMap;
    float  useEmissiveMap;

    float  lockOnIntensity;
    float  pulseSpeed;
    float4 rimColor;
    float  rimPower;
    float3 _pad;
};

Texture2D AlbedoMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
Texture2D NormalMap : register(REG_SRV(SLOT_SRV_MAT_TEX1));
Texture2D MetalRoughMap : register(REG_SRV(SLOT_SRV_MAT_TEX2));
Texture2D EmissiveMap : register(REG_SRV(SLOT_SRV_MAT_TEX3));

SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    PBRSurfaceInfo surf;
    surf.pos = pin.position.xyz;
    surf.viewDir = normalize(cameraPosition.xyz - surf.pos);

    if (useNormalMap > 0.5f)
        surf.normal = CalculateWorldNormal(pin.normal, pin.tangent, pin.texcoord, NormalMap, LinearSampler);
    else
        surf.normal = normalize(pin.normal);

    float4 albedoTex = useAlbedoMap > 0.5f ? AlbedoMap.Sample(LinearSampler, pin.texcoord) : float4(1, 1, 1, 1);
    surf.albedo = albedoTex.rgb * baseColor.rgb;
    float alpha = albedoTex.a * baseColor.a;

    float4 mrSample = useMetalRoughMap > 0.5f ? MetalRoughMap.Sample(LinearSampler, pin.texcoord) : float4(1, 1, 1, 1);
    surf.metalness = metalness * mrSample.b;
    surf.roughness = roughness * mrSample.g;
    float rawAO = useOcclusionMap > 0.5f ? mrSample.r : 1.0f;
    surf.ao = lerp(1.0f, rawAO, occlusionStrength);
    surf.f0 = lerp(float3(0.04f, 0.04f, 0.04f), surf.albedo, surf.metalness);

    float viewDepth = length(cameraPosition.xyz - pin.position.xyz);
    float shadowWeight = CalculateShadowWeight(pin.position, viewDepth);

    LightResult light = CalculateAllLightsPBR(surf, shadowWeight);
    float3 ambientIBL = CalculateIBL(surf);
    ambientIBL *= lerp(0.2f, 1.0f, shadowWeight);

    float3 emissive = useEmissiveMap > 0.5f ? EmissiveMap.Sample(LinearSampler, pin.texcoord).rgb * emissiveColor : float3(0.0f, 0.0f, 0.0f);
    float3 finalColor = ambientIBL + light.diffuse + light.specular + emissive;

    if (lockOnIntensity > 0.0f)
    {
        float pulse = (pulseSpeed > 0.0f) ? Pulse(0.5f, 1.0f, pulseSpeed) : 1.0f;
        finalColor = ApplyRimLightParams(finalColor, surf.normal, surf.viewDir,
                                         rimPower, rimColor.rgb, lockOnIntensity * pulse);
    }

    finalColor = ApplyHeightFog(surf.pos, cameraPosition.xyz, finalColor);

    return float4(finalColor, alpha);
}
