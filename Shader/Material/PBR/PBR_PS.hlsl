// Shader/Material/PBR/PBR_PS.hlsl
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/PBRLighting.hlsli" // 物理演算エンジンをインクルード

// --- 定数バッファ & リソース ---
cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 baseColor;
    float3 emissiveColor;
    float metalness;
    float roughness;
    float occlusionStrength;
};

Texture2D AlbedoMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
Texture2D NormalMap : register(REG_SRV(SLOT_SRV_MAT_TEX1));
Texture2D MetalRoughMap : register(REG_SRV(SLOT_SRV_MAT_TEX2)); 
// ※PBRでは4枚目が必要なため、パーティクル用のスロットを間借りするか、SrvMaterialTex3をヘッダに足すのが理想です
Texture2D EmissiveMap : register(REG_SRV(SLOT_SRV_PARTICLE_BUF)); 

SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

// ==============================================================================
// メイン関数
// ==============================================================================
float4 main(VS_OUT pin) : SV_TARGET
{
    // ------------------------------------------------------------
    // 1. 下ごしらえ：PBR表面情報 (PBRSurfaceInfo) の構築
    // ------------------------------------------------------------
    PBRSurfaceInfo surf;
    surf.pos = pin.position.xyz;
    surf.viewDir = normalize(cameraPosition.xyz - surf.pos);
    surf.normal = CalculateWorldNormal(pin.normal, pin.tangent, pin.texcoord, NormalMap, LinearSampler);

    // テクスチャサンプリング
    float4 sampledColor = AlbedoMap.Sample(LinearSampler, pin.texcoord) * baseColor;
    surf.albedo = sampledColor.rgb;
    float alpha = sampledColor.a;

    float4 mrSample = MetalRoughMap.Sample(LinearSampler, pin.texcoord);
    surf.metalness = metalness * mrSample.b;
    surf.roughness = roughness * mrSample.g;
    surf.ao = lerp(1.0f, mrSample.r, occlusionStrength);

    // F0の決定 (非金属は0.04、金属はアルベド色)
    surf.f0 = lerp(float3(0.04, 0.04, 0.04), surf.albedo, surf.metalness);

    // ------------------------------------------------------------
    // 2. 調理：環境光と直接光の評価を委譲
    // ------------------------------------------------------------
    float shadowWeight = CalculateShadowWeight(pin.shadowPos);
    
    // 環境光
    float hemiWeight = surf.normal.y * 0.5f + 0.5f;
    float3 ambient = lerp(gDirLight.gGroundColor.rgb * gDirLight.gGroundColor.a, gDirLight.gSkyColor.rgb * gDirLight.gSkyColor.a, hemiWeight);
    ambient *= surf.albedo * (1.0 - surf.metalness) * surf.ao;

    // 全光源のPBR計算を実行
    LightResult light = CalculateAllLightsPBR(surf, shadowWeight);

    // ------------------------------------------------------------
    // 3. 盛り付け：最終合成
    // ------------------------------------------------------------
    float3 emissive = EmissiveMap.Sample(LinearSampler, pin.texcoord).rgb * emissiveColor;
    float3 finalColor = ambient + light.diffuse + light.specular + emissive;
    
    finalColor = ApplyShadowColor(finalColor, shadowWeight);

    // リムライト
    if (gFogRimParams.y > 0.0f)
    {
        float NdotV = saturate(dot(surf.normal, surf.viewDir));
        float rimFactor = pow(1.0f - NdotV, gFogRimParams.x);
        float curvature = length(fwidth(surf.normal));
        float flatMask = saturate(curvature * 100.0f);
        float floorMask = 1.0f - step(0.9f, surf.normal.y);
        
        finalColor += rimFactor * gFogRimColor.rgb * gFogRimParams.y * flatMask * floorMask;
    }
    
    // フォグ
    finalColor = ApplyHeightFog(surf.pos, cameraPosition.xyz, finalColor, LinearSampler, surf.normal);

    return float4(finalColor, alpha);
}