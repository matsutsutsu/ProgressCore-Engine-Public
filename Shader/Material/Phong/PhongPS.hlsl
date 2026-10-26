// Shader/Material/Phong/PhongPS.hlsl
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Lighting.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 baseColor;
};

Texture2D AlbedoMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
Texture2D NormalMap : register(REG_SRV(SLOT_SRV_MAT_TEX1));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    // ① 下ごしらえ
    float flashIntensity = pin.customParams.z;

    SurfaceInfo surf;
    float4 sampledColor = AlbedoMap.Sample(LinearSampler, pin.texcoord) * baseColor;
    surf.color = sampledColor.rgb;
    surf.alpha = sampledColor.a;
    surf.pos = pin.position.xyz;
    surf.viewDir = normalize(cameraPosition.xyz - surf.pos);
    surf.normal = CalculateWorldNormal(pin.normal, pin.tangent, pin.texcoord, NormalMap, LinearSampler);

    // ② 光の調理
    float viewDepth = length(cameraPosition.xyz - pin.position.xyz);
    float shadowWeight = CalculateShadowWeight(pin.position, viewDepth);
    float hemiWeight = surf.normal.y * 0.5f + 0.5f;
    float3 ambient = lerp(gDirLight.gGroundColor.rgb * gDirLight.gGroundColor.a, gDirLight.gSkyColor.rgb * gDirLight.gSkyColor.a, hemiWeight);

    LightResult light = CalculateAllLights(surf, shadowWeight);
    float3 finalColor = surf.color * (light.diffuse + ambient) + light.specular;

    // ③ 飾り付け (エフェクトの適用)
    finalColor = ApplyHeightFog(surf.pos, cameraPosition.xyz, finalColor);
    finalColor = ApplyHitFlash(finalColor, flashIntensity);

    return float4(finalColor, surf.alpha);
}