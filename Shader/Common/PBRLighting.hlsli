// Shader/Common/PBRLighting.hlsli
#ifndef PBR_LIGHTING_HLSLI
#define PBR_LIGHTING_HLSLI

#include "Shader/Common/Lighting.hlsli"

static const float PI = 3.14159265359f;

// ---------------------------------------------------------
// PBR用の表面情報構造体
// ---------------------------------------------------------
struct PBRSurfaceInfo
{
    float3 pos;
    float3 normal;
    float3 viewDir;
    float3 albedo;
    float roughness;
    float metalness;
    float3 f0; // 垂直入射時の反射率
    float ao; // アンビエントオクルージョン
};

// ==========================================================
// Cook-Torrance BRDF 関数群 (カプセル化して隠蔽)
// ==========================================================
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(float3 N, float3 H, float rough)
{
    float a = rough * rough;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / max(denom, 0.0000001);
}

float GeometrySchlickGGX(float NdotV, float rough)
{
    float r = (rough + 1.0);
    float k = (r * r) / 8.0;
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float rough)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, rough);
    float ggx1 = GeometrySchlickGGX(NdotL, rough);
    return ggx1 * ggx2;
}

// ---------------------------------------------------------
// 1光源に対するPBR計算
// ---------------------------------------------------------
LightResult CalcPBRLight(PBRSurfaceInfo surf, float3 L, float3 radiance)
{
    LightResult res;
    res.diffuse = float3(0, 0, 0);
    res.specular = float3(0, 0, 0);

    float3 H = normalize(surf.viewDir + L);
    float NdotL = max(dot(surf.normal, L), 0.0);
    float NdotV = max(dot(surf.normal, surf.viewDir), 0.0);

    if (NdotL > 0.0)
    {
        float NDF = DistributionGGX(surf.normal, H, surf.roughness);
        float G = GeometrySmith(surf.normal, surf.viewDir, L, surf.roughness);
        float3 F = FresnelSchlick(max(dot(H, surf.viewDir), 0.0), surf.f0);

        float3 nominator = NDF * G * F;
        float denominator = 4.0 * NdotV * NdotL + 0.001;
        
        res.specular = (nominator / denominator) * radiance * NdotL;

        float3 kS = F;
        float3 kD = float3(1.0, 1.0, 1.0) - kS;
        kD *= 1.0 - surf.metalness;

        res.diffuse = (kD * surf.albedo / PI) * radiance * NdotL;
    }
    return res;
}

// ==========================================================
// 全ライト統合計算
// ==========================================================
LightResult CalculateAllLightsPBR(PBRSurfaceInfo surf, float shadowWeight)
{
    LightResult totalRes;
    totalRes.diffuse = float3(0, 0, 0);
    totalRes.specular = float3(0, 0, 0);

    // 1. 平行光源
    {
        float3 L = normalize(-gDirLight.direction);
        float3 radiance = gDirLight.color * gDirLight.intensity;
        LightResult res = CalcPBRLight(surf, L, radiance);
        totalRes.diffuse += res.diffuse * shadowWeight;
        totalRes.specular += res.specular * shadowWeight;
    }

    // 2. 点光源
    for (int i = 0; i < gPointLightCount; ++i)
    {
        if (gPointLights[i].active < 0.5f)
            continue;
        float3 L = gPointLights[i].position - surf.pos;
        float d = length(L);
        L /= d;

        if (d < gPointLights[i].range)
        {
            float atten = 1.0f / (gPointLights[i].constantAttenuation + gPointLights[i].linearAttenuation * d + gPointLights[i].quadraticAttenuation * (d * d));
            float fade = saturate(1.0f - d / gPointLights[i].range);
            float3 radiance = gPointLights[i].color * gPointLights[i].intensity * atten * fade;
            
            LightResult res = CalcPBRLight(surf, L, radiance);
            totalRes.diffuse += res.diffuse;
            totalRes.specular += res.specular;
        }
    }

    // 3. スポットライト
    for (int j = 0; j < gSpotLightCount; ++j)
    {
        if (gSpotLights[j].active < 0.5f)
            continue;
        float3 L = gSpotLights[j].position - surf.pos;
        float d = length(L);
        L /= d;

        if (d < gSpotLights[j].range)
        {
            float cosAngle = dot(normalize(gSpotLights[j].direction), -L);
            float angleAtten = saturate((cosAngle - gSpotLights[j].outerCos) / (gSpotLights[j].innerCos - gSpotLights[j].outerCos));
            float distAtten = 1.0f / (gSpotLights[j].constantAttenuation + gSpotLights[j].linearAttenuation * d + gSpotLights[j].quadraticAttenuation * (d * d));
            float fade = saturate(1.0f - d / gSpotLights[j].range);
            
            float3 radiance = gSpotLights[j].color * gSpotLights[j].intensity * angleAtten * distAtten * fade;
            
            LightResult res = CalcPBRLight(surf, L, radiance);
            totalRes.diffuse += res.diffuse;
            totalRes.specular += res.specular;
        }
    }

    return totalRes;
}

#endif // PBR_LIGHTING_HLSLI