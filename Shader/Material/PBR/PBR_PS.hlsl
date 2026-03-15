#include "Shader/Common/Common.hlsli"

// --- マテリアル定数バッファ ---
cbuffer CbMaterial : register(b0)
{
    float4 baseColor; // アルベド色
    float3 emissiveColor; // 発光色
    float metalness; // 金属度 (0=絶縁体, 1=金属)
    float roughness; // 粗さ (0=ツルツル, 1=ザラザラ)
    float occlusionStrength; // AOの強さ
};

// --- テクスチャ定義 ---
Texture2D AlbedoMap : register(t0);
Texture2D NormalMap : register(t1);
Texture2D MetalRoughMap : register(t2); // R=AO, G=Roughness, B=Metal
Texture2D EmissiveMap : register(t3);

SamplerState LinearSampler : register(s0);

static const float PI = 3.14159265359f;

// ==============================================================================
// PBR 物理計算関数群 (Cook-Torrance BRDF)
// ==============================================================================
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

// ------------------------------------------------------------------------------
// 1つの光源に対する PBRライティング計算 (Diffuse と Specular を分けて返す)
// ------------------------------------------------------------------------------
LightResult CalcPBRLight(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float rough, float metal, float3 F0)
{
    LightResult res;
    res.diffuse = float3(0, 0, 0);
    res.specular = float3(0, 0, 0);

    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    if (NdotL > 0.0)
    {
        // クック・トランス BRDF
        float NDF = DistributionGGX(N, H, rough);
        float G = GeometrySmith(N, V, L, rough);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
           
        float3 nominator = NDF * G * F;
        float denominator = 4.0 * NdotV * NdotL + 0.001;
        
        // 鏡面反射 (Specular)
        res.specular = (nominator / denominator) * radiance * NdotL;
        
        // 拡散反射 (Diffuse) - エネルギー保存則を適用
        float3 kS = F;
        float3 kD = float3(1.0, 1.0, 1.0) - kS;
        kD *= 1.0 - metal; // 金属は光を内部に透過させない

        res.diffuse = (kD * albedo / PI) * radiance * NdotL;
    }
    return res;
}

// ------------------------------------------------------------------------------
// 全てのライト (平行・点・スポット) をループ計算する PBR版関数
// ------------------------------------------------------------------------------
LightResult CalculateAllLightsPBR(SurfaceInfo surf, float shadowWeight, float rough, float metal, float3 F0)
{
    LightResult totalRes;
    totalRes.diffuse = float3(0, 0, 0);
    totalRes.specular = float3(0, 0, 0);

    // 1. 平行光源 (Directional Light)
    {
        float3 L = normalize(-gDirLight.direction);
        float3 radiance = gDirLight.color * gDirLight.intensity;
        
        LightResult res = CalcPBRLight(surf.normal, surf.viewDir, L, radiance, surf.color, rough, metal, F0);
        totalRes.diffuse += res.diffuse * shadowWeight;
        totalRes.specular += res.specular * shadowWeight;
    }

    // 2. 点光源 (Point Light)
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

            LightResult res = CalcPBRLight(surf.normal, surf.viewDir, L, radiance, surf.color, rough, metal, F0);
            totalRes.diffuse += res.diffuse;
            totalRes.specular += res.specular;
        }
    }

    // 3. スポットライト (Spot Light)
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

            LightResult res = CalcPBRLight(surf.normal, surf.viewDir, L, radiance, surf.color, rough, metal, F0);
            totalRes.diffuse += res.diffuse;
            totalRes.specular += res.specular;
        }
    }

    return totalRes;
}

// ==============================================================================
// メイン関数 (PhongPS の構造を踏襲)
// ==============================================================================
float4 main(VS_OUT pin) : SV_TARGET
{
    // ------------------------------------------------------------
    // 1. 表面情報 (SurfaceInfo) の構築
    // ------------------------------------------------------------
    SurfaceInfo surf;
    
    // 基本色
    float4 sampledColor = AlbedoMap.Sample(LinearSampler, pin.texcoord) * baseColor;
    surf.color = sampledColor.rgb;
    surf.alpha = sampledColor.a;
    surf.pos = pin.position.xyz;
    surf.viewDir = normalize(cameraPosition.xyz - surf.pos);
    surf.normal = CalculateWorldNormal(pin.normal, pin.tangent, pin.texcoord, NormalMap, LinearSampler);

    // PBRパラメータ抽出 (glTF標準: R=AO, G=Roughness, B=Metalness)
    float4 mrSample = MetalRoughMap.Sample(LinearSampler, pin.texcoord);
    float metal = metalness * mrSample.b;
    float rough = roughness * mrSample.g;
    float ao = lerp(1.0f, mrSample.r, occlusionStrength); // RチャンネルのAOを適用
    float3 emissive = EmissiveMap.Sample(LinearSampler, pin.texcoord).rgb * emissiveColor;

    // F0 (垂直入射時の反射率) の計算
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, surf.color, metal);

    // ------------------------------------------------------------
    // 2. 影と環境光の計算
    // ------------------------------------------------------------
    float shadowWeight = CalculateShadowWeight(pin.shadowPos);
    float hemiWeight = surf.normal.y * 0.5f + 0.5f;
    
    // 環境光 (半球ライティング) に AOマップ を適用
    float3 ambient = lerp(gDirLight.gGroundColor.rgb * gDirLight.gGroundColor.a, gDirLight.gSkyColor.rgb * gDirLight.gSkyColor.a, hemiWeight);
    ambient *= surf.color * (1.0 - metal) * ao;

    // ------------------------------------------------------------
    // 3. 統合ライティングの実行
    // ------------------------------------------------------------
    LightResult light = CalculateAllLightsPBR(surf, shadowWeight, rough, metal, F0);

    // ------------------------------------------------------------
    // 4. 最終色の合成
    // ------------------------------------------------------------
    float3 finalColor = ambient + light.diffuse + light.specular + emissive;

    // 影の色味を適用 (PhongPSと同等)
    finalColor = ApplyShadowColor(finalColor, shadowWeight);
    
    // ------------------------------------------------------------
    // リムライト (Rim Light) ※PhongPSから完全移植
    // ------------------------------------------------------------
    if (gFogRimParams.y > 0.0f)
    {
        float NdotV = saturate(dot(surf.normal, surf.viewDir));
        float rimFactor = pow(1.0f - NdotV, gFogRimParams.x);
        
        float curvature = length(fwidth(surf.normal));
        float flatMask = saturate(curvature * 100.0f);
        float floorMask = 1.0f - step(0.9f, surf.normal.y);
        
        float3 rimLight = rimFactor * gFogRimColor.rgb * gFogRimParams.y * flatMask * floorMask;
        finalColor += rimLight;
    }
    
    // ------------------------------------------------------------
    // 5. 後処理 (フォグ) ※PhongPSから完全移植
    // ------------------------------------------------------------
    finalColor = ApplyHeightFog(surf.pos, cameraPosition.xyz, finalColor, LinearSampler, surf.normal);

    return float4(finalColor, surf.alpha);
}