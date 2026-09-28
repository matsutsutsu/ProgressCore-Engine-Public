// Shader/Material/GhostPBR/GhostPBRPS.hlsl
//
// ゴースト(半透明キャラクター)用のピクセルシェーダー。
// PBR_PS.hlsl の陰影計算(IBL・直接光・リムライト・フォグ)を丸ごと維持し、
// 追加した ghostAlpha 1つだけで透明度を調整できるようにしたもの。
// 「見た目はPBRのまま、最後に透明度を掛けるだけ」というのがゴーストの本質。
//
// PSO側(shaders.json: "blend":"transparency", "depth":"testOnly")と組み合わせて
// 初めて「ゴースト同士が重なっても互いを完全に隠さない」という破綻回避が効く。
// このシェーダー単体はPBRと同じ色計算をしているだけで、透明合成自体はPSOの仕事。
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/PBRLighting.hlsli"

// ==============================================================================
// 定数バッファ & リソース（PBR_PS.hlsl と同一 + ghostAlpha を末尾に追加）
// ==============================================================================
cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 baseColor;
    float3 emissiveColor;
    float metalness;
    float roughness;
    float occlusionStrength;

    // C++側から送られてくる「テクスチャの有無」フラグ
    float useAlbedoMap;
    float useNormalMap;
    float useMetalRoughMap;
    float useOcclusionMap;
    float useEmissiveMap;

    // ゴーストとしての透明度。0=完全に透明、1=不透明相当。
    //   目安: 0.3〜0.5 で「幽霊らしい」薄さになる。
    //   ※ "Fill All Missing With Defaults" は一律1.0を入れるため、
    //      Ghostとして使うなら追加後に必ずこの値を下げること。
    float ghostAlpha;
};

Texture2D AlbedoMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
Texture2D NormalMap : register(REG_SRV(SLOT_SRV_MAT_TEX1));
Texture2D MetalRoughMap : register(REG_SRV(SLOT_SRV_MAT_TEX2));
Texture2D EmissiveMap : register(REG_SRV(SLOT_SRV_MAT_TEX3));

SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

// ==============================================================================
// メイン関数（PBR_PS.hlsl と同一ロジック。最終行の alpha 計算のみ変更）
// ==============================================================================
float4 main(VS_OUT pin) : SV_TARGET
{
    PBRSurfaceInfo surf;
    surf.pos = pin.position.xyz;
    surf.viewDir = normalize(cameraPosition.xyz - surf.pos);

    // ------------------------------------------------------------
    // 1. テクスチャのサンプリングとフォールバック処理
    // ------------------------------------------------------------
    if (useNormalMap > 0.5f)
    {
        surf.normal = CalculateWorldNormal(pin.normal, pin.tangent, pin.texcoord, NormalMap, LinearSampler);
    }
    else
    {
        surf.normal = normalize(pin.normal);
    }

    float4 albedoTex = useAlbedoMap > 0.5f ? AlbedoMap.Sample(LinearSampler, pin.texcoord) : float4(1.0f, 1.0f, 1.0f, 1.0f);
    surf.albedo = albedoTex.rgb * baseColor.rgb;
    float alpha = albedoTex.a * baseColor.a;

    float4 mrSample = useMetalRoughMap > 0.5f ? MetalRoughMap.Sample(LinearSampler, pin.texcoord) : float4(1.0f, 1.0f, 1.0f, 1.0f);

    surf.metalness = metalness * mrSample.b;
    surf.roughness = roughness * mrSample.g;
    float rawAO = useOcclusionMap > 0.5f ? mrSample.r : 1.0f;
    surf.ao = lerp(1.0f, rawAO, occlusionStrength);

    surf.f0 = lerp(float3(0.04f, 0.04f, 0.04f), surf.albedo, surf.metalness);

    // ------------------------------------------------------------
    // 2. 光の計算（物理演算）
    // ------------------------------------------------------------
    float viewDepth = length(cameraPosition.xyz - pin.position.xyz);
    float shadowWeight = CalculateShadowWeight(pin.position, viewDepth);

    LightResult light = CalculateAllLightsPBR(surf, shadowWeight);
    float3 ambientIBL = CalculateIBL(surf);

    float skyOcclusion = lerp(0.2f, 1.0f, shadowWeight);
    ambientIBL *= skyOcclusion;

    // ------------------------------------------------------------
    // 3. 最終合成とエフェクト
    // ------------------------------------------------------------
    float3 emissive = useEmissiveMap > 0.5f ? EmissiveMap.Sample(LinearSampler, pin.texcoord).rgb * emissiveColor : float3(0.0f, 0.0f, 0.0f);
    float3 finalColor = ambientIBL + light.diffuse + light.specular + emissive;

    if (gFogRimParams.y > 0.0f)
    {
        float NdotV = saturate(dot(surf.normal, surf.viewDir));
        float rimFactor = pow(1.0f - NdotV, gFogRimParams.x);
        float curvature = length(fwidth(surf.normal));
        float flatMask = saturate(curvature * 100.0f);
        float floorMask = 1.0f - step(0.9f, surf.normal.y);

        finalColor += rimFactor * gFogRimColor.rgb * gFogRimParams.y * flatMask * floorMask;
    }

    finalColor = ApplyHeightFog(surf.pos, cameraPosition.xyz, finalColor);

    // ここだけがPBR_PS.hlslとの違い: 最終アルファに ghostAlpha を掛ける。
    //   色計算(finalColor)自体は完全に不透明PBRと同じで、
    //   「薄くなる」のはPSOの半透明合成とこのアルファ値の掛け算による。
    return float4(finalColor, alpha * ghostAlpha);
}
