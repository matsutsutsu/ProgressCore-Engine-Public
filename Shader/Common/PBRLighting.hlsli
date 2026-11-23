// ===========================================================================
// 概要: PBR のライティング。直接光は Cook-Torrance、環境光は IBL（事前に焼いた3枚のテクスチャ）で求める
//
// 備考: ここの関数の L は「面からライトへの向き」。Lighting.hlsli の L とは逆向き
// ===========================================================================
#ifndef PBR_LIGHTING_HLSLI
#define PBR_LIGHTING_HLSLI

#include "Shader/Common/Lighting.hlsli"
#include "Engine/Graphics/Shader/ShaderResources.h"

static const float PI = 3.14159265359f;

struct PBRSurfaceInfo
{
    float3 pos;       // ワールド座標
    float3 normal;    // 法線（法線マップ適用済み）
    float3 viewDir;   // 面から視点への向き
    float3 albedo;    // 面の色
    float roughness;  // 粗さ（0 で鏡、1 でざらざら）
    float metalness;  // 金属らしさ（0〜1）
    float3 f0;        // 真正面から見たときの反射率
    float ao;         // 環境光の遮蔽（0〜1。1 で遮られていない）
};

// ---------------------------------------------------------------------------
// Cook-Torrance の3つの項
// ---------------------------------------------------------------------------

// F: フレネル（Schlick の近似式）。斜めから見るほど強く反射する
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    // 1 - cosTheta は誤差で負になることがあり、そのまま 5 乗すると NaN になる
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// IBL 用のフレネル。粗い面ほど、縁の反射を弱める
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 r = float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness);
    return F0 + (max(r, F0) - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// D: 微小な面の向きの分布（GGX）。H を向く面がどれだけあるか
float DistributionGGX(float3 N, float3 H, float rough)
{
    float a = rough * rough;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0000001f);
}

// G: 微小な面どうしの影と隠れ（Schlick-GGX）。直接光の k = (粗さ + 1)^2 / 8 を使う
float GeometrySchlickGGX(float NdotV, float rough)
{
    float r = (rough + 1.0f);
    float k = (r * r) / 8.0f;

    float denom = NdotV * (1.0f - k) + k;
    return NdotV / denom;
}

// G を視線側とライト側の両方で求めて掛ける（Smith）
float GeometrySmith(float3 N, float3 V, float3 L, float rough)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);

    float ggx2 = GeometrySchlickGGX(NdotV, rough);
    float ggx1 = GeometrySchlickGGX(NdotL, rough);

    return ggx1 * ggx2;
}

// ---------------------------------------------------------------------------
// 直接光
// ---------------------------------------------------------------------------

// ライト1つ分の拡散と鏡面。radiance はその面に届く光の色と強さ
LightResult CalcPBRLight(PBRSurfaceInfo surf, float3 L, float3 radiance)
{
    LightResult res = { float3(0, 0, 0), float3(0, 0, 0) };

    float3 H = normalize(surf.viewDir + L);
    float NdotL = max(dot(surf.normal, L), 0.0f);
    float NdotV = max(dot(surf.normal, surf.viewDir), 0.0f);

    if (NdotL > 0.0f)
    {
        float NDF = DistributionGGX(surf.normal, H, surf.roughness);
        float G = GeometrySmith(surf.normal, surf.viewDir, L, surf.roughness);
        float3 F = FresnelSchlick(max(dot(H, surf.viewDir), 0.0f), surf.f0);

        // 鏡面 = D * G * F / (4 * NdotV * NdotL)。分母の 0.001 は 0 で割らないため
        float3 nominator = NDF * G * F;
        float denominator = 4.0f * NdotV * NdotL + 0.001f;
        res.specular = (nominator / denominator) * radiance * NdotL;

        // 拡散。鏡面で反射した分（F）と、金属の分は拡散しない
        float3 kD = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - surf.metalness);
        res.diffuse = (kD * surf.albedo / PI) * radiance * NdotL;
    }
    return res;
}

// 平行光源・点光源・スポットライトの全部を足す。影（shadowWeight）は平行光源にだけ掛ける
LightResult CalculateAllLightsPBR(PBRSurfaceInfo surf, float shadowWeight)
{
    LightResult totalRes = { float3(0, 0, 0), float3(0, 0, 0) };

    // 平行光源
    {
        float3 L = normalize(-gDirLight.direction);
        float3 radiance = gDirLight.color * gDirLight.intensity;
        LightResult res = CalcPBRLight(surf, L, radiance);

        totalRes.diffuse += res.diffuse * shadowWeight;
        totalRes.specular += res.specular * shadowWeight;
    }

    // 点光源
    for (int i = 0; i < gPointLightCount; ++i)
    {
        if (gPointLights[i].active < 0.5f)
            continue;

        float3 L = gPointLights[i].position - surf.pos;
        float d = length(L);
        L /= d;

        if (d < gPointLights[i].range)
        {
            // 距離の減衰に加え、範囲の端で急に消えないよう、端へ向けて 0 に落とす
            float atten = 1.0f / (gPointLights[i].constantAttenuation + gPointLights[i].linearAttenuation * d + gPointLights[i].quadraticAttenuation * (d * d));
            float fade = saturate(1.0f - d / gPointLights[i].range);
            float3 radiance = gPointLights[i].color * gPointLights[i].intensity * atten * fade;

            LightResult res = CalcPBRLight(surf, L, radiance);
            totalRes.diffuse += res.diffuse;
            totalRes.specular += res.specular;
        }
    }

    // スポットライト
    for (int j = 0; j < gSpotLightCount; ++j)
    {
        if (gSpotLights[j].active < 0.5f)
            continue;

        float3 L = gSpotLights[j].position - surf.pos;
        float d = length(L);
        L /= d;

        if (d < gSpotLights[j].range)
        {
            // 角度の減衰（内側の角までは 1、外側の角で 0）・距離の減衰・範囲の端のフェード
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

// ---------------------------------------------------------------------------
// 環境光（IBL）
// ---------------------------------------------------------------------------

TextureCube gIrradianceMap : register(REG_SRV(SLOT_SRV_IBL_IRRADIANCE)); // 向きごとの、周りから届く光の平均（拡散用）
TextureCube gPrefilterMap : register(REG_SRV(SLOT_SRV_IBL_PREFILTER));   // 粗さごとにぼかした周りの景色（ミップ = 粗さ）
Texture2D gBRDFLUT : register(REG_SRV(SLOT_SRV_IBL_BRDFLUT));            // (NdotV, 粗さ) から引く、鏡面の倍率と足す量
SamplerState gSamplerIBL : register(REG_SMP(SLOT_SMP_IBL));

float3 CalculateIBL(PBRSurfaceInfo surf)
{
    float NdotV = saturate(dot(surf.normal, surf.viewDir));
    float3 R = reflect(-surf.viewDir, surf.normal); // 視線が面で跳ね返る向き

    float3 F = FresnelSchlickRoughness(NdotV, surf.f0, surf.roughness);

    // 拡散。法線の向きから届く光の平均に、面の色を掛ける
    float3 kD = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - surf.metalness);
    float3 irradiance = gIrradianceMap.Sample(gSamplerIBL, surf.normal).rgb;
    float3 diffuseIBL = irradiance * surf.albedo * kD;

    // 鏡面。反射の向きの景色を、粗さに合ったぼけ具合（ミップ）で読む
    const float MAX_REFLECTION_LOD = 4.0f; // gPrefilterMap の一番粗いミップの番号
    float3 prefilteredColor = gPrefilterMap.SampleLevel(gSamplerIBL, R, surf.roughness * MAX_REFLECTION_LOD).rgb;
    float2 envBRDF = gBRDFLUT.Sample(gSamplerIBL, float2(NdotV, surf.roughness)).rg;

    float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    return (diffuseIBL + specularIBL) * surf.ao;
}

#endif // PBR_LIGHTING_HLSLI