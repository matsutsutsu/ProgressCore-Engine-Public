#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

// ===========================================================================
// 概要: Lambert / Phong のマテリアルが使う、昔ながらのライティングの関数
//
// 備考: ここの関数の L は「光が進む向き」（ライト → 面）。PBRLighting.hlsli の L は逆向き（面 → ライト）
//       ライトの構造体と cbuffer は ShaderShared.h で C++ と共有している
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

// ライティングの結果。拡散と鏡面を分けて返し、呼ぶ側で色を掛けてから足す
struct LightResult
{
    float3 diffuse;
    float3 specular;
};

struct SurfaceInfo
{
    float3 pos;     // ワールド座標
    float3 normal;  // 法線（法線マップ適用済み）
    float3 viewDir; // 面から視点への向き
    float3 color;   // テクスチャ × マテリアルの色
    float  alpha;   // 不透明度
};

// Lambert の拡散反射。N は法線、L は光が進む向き、C は光の色と強さ、K は反射率
float3 CalcLambert(float3 N, float3 L, float3 C, float3 K)
{
    return C * saturate(dot(N, -L)) * K;
}

// 光の当たり具合（0〜1）だけを返す。色を掛けずに段階を付けたいマテリアル用
float CalcLightIntensity(float3 N, float3 L)
{
    return saturate(dot(N, -L));
}

// Phong の鏡面反射。E は面から視点への向き。光沢の鋭さ（指数）は 128 で固定
float3 CalcPhongSpecular(float3 N, float3 L, float3 E, float3 C, float3 K)
{
    float3 R = reflect(L, N);
    return C * pow(max(dot(E, R), 0), 128) * K;
}

// 半球ライティング。法線が up を向くほど空の色、逆を向くほど地面の色にする。強さは hemisphere_weight.x
float3 CalcHemiSphereLight(float3 normal, float3 up, float3 sky_color, float3 ground_color, float4 hemisphere_weight)
{
    float factor = dot(normal, up) * 0.5f + 0.5f;
    return lerp(ground_color, sky_color, factor) * hemisphere_weight.x;
}

// 法線マップの値を、ワールド空間の法線に直す
float3 CalculateWorldNormal(
    float3 worldNormal,
    float4 worldTangent,
    float2 texcoord,
    Texture2D normalMap,
    SamplerState smp)
{
    // 接線を法線と直交させてから、従法線を作る（w は UV の向きで決まる従法線の符号）
    float3 N = normalize(worldNormal);
    float3 T = normalize(worldTangent.xyz);
    T = normalize(T - dot(T, N) * N);
    float3 B = normalize(cross(N, T) * worldTangent.w);

    float3 normalSample = normalMap.Sample(smp, texcoord).xyz * 2.0f - 1.0f;

    return normalize((normalSample.x * T) + (normalSample.y * B) + (normalSample.z * N));
}



// 平行光源・点光源・スポットライトの全部を足す。影（shadowWeight）は平行光源にだけ掛ける
LightResult CalculateAllLights(SurfaceInfo surf, float shadowWeight)
{
    LightResult res;
    res.diffuse = 0;
    res.specular = 0;

    // 平行光源
    {
        float3 L = normalize(gDirLight.direction);
        float3 LC = gDirLight.color * gDirLight.intensity;
        res.diffuse += CalcLambert(surf.normal, L, LC, 1.0f) * shadowWeight;
        res.specular += CalcPhongSpecular(surf.normal, L, surf.viewDir, LC, 1.0f) * shadowWeight;
    }

    // 点光源
    for (int i = 0; i < gPointLightCount; ++i)
    {
        if (gPointLights[i].active < 0.5f)
            continue;
        float3 L = surf.pos - gPointLights[i].position;
        float d = length(L);
        L /= d;
        if (d < gPointLights[i].range)
        {
            // 距離の減衰に加え、範囲の端で急に消えないよう、端へ向けて 0 に落とす
            float atten = 1.0f / (gPointLights[i].constantAttenuation + gPointLights[i].linearAttenuation * d + gPointLights[i].quadraticAttenuation * (d * d));
            float3 LC = gPointLights[i].color * gPointLights[i].intensity * atten * saturate(1.0f - d / gPointLights[i].range);
            res.diffuse += CalcLambert(surf.normal, L, LC, 1.0f);
            res.specular += CalcPhongSpecular(surf.normal, L, surf.viewDir, LC, 1.0f);
        }
    }

    // スポットライト
    for (int j = 0; j < gSpotLightCount; ++j)
    {
        if (gSpotLights[j].active < 0.5f)
            continue;

        float3 L = surf.pos - gSpotLights[j].position;
        float d = length(L);
        L /= d;

        if (d < gSpotLights[j].range)
        {
            // 角度の減衰。内側の角（innerCos）までは 1、外側の角（outerCos）で 0
            float cosAngle = dot(normalize(gSpotLights[j].direction), L);
            float angleAtten = saturate((cosAngle - gSpotLights[j].outerCos) / (gSpotLights[j].innerCos - gSpotLights[j].outerCos));

            // 距離の減衰（定数・1次・2次の3つの係数）
            float distAtten = 1.0f / (gSpotLights[j].constantAttenuation +
                                      gSpotLights[j].linearAttenuation * d +
                                      gSpotLights[j].quadraticAttenuation * (d * d));

            // 点光源と同じく、範囲の端へ向けて 0 に落とす
            float3 LC = gSpotLights[j].color * gSpotLights[j].intensity * angleAtten * distAtten * saturate(1.0f - d / gSpotLights[j].range);

            res.diffuse += CalcLambert(surf.normal, L, LC, 1.0f);
            res.specular += CalcPhongSpecular(surf.normal, L, surf.viewDir, LC, 1.0f);
        }
    }
    return res;
}


#endif // LIGHTING_HLSLI