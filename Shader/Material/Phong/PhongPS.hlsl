// ===========================================================================
// 概要: Phong マテリアル。拡散・鏡面・半球の環境光で照らし、フォグと被弾の白い点滅を掛ける
//
// 入力: PhongVS の出力（UV・法線・接線・ワールド座標・customParams）
// 出力: 色（α 付き）
//
// 備考: customParams.z を白い点滅の強さ（0〜1）として読む
//       PBR と違ってテクスチャがあるかの印を持たず、法線マップも必ず読む
// ===========================================================================
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Lighting.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 baseColor; // テクスチャに掛ける色
};

Texture2D AlbedoMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
Texture2D NormalMap : register(REG_SRV(SLOT_SRV_MAT_TEX1));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    float flashIntensity = pin.customParams.z;

    SurfaceInfo surf;
    float4 sampledColor = AlbedoMap.Sample(LinearSampler, pin.texcoord) * baseColor;
    surf.color = sampledColor.rgb;
    surf.alpha = sampledColor.a;
    surf.pos = pin.position.xyz;
    surf.viewDir = normalize(cameraPosition.xyz - surf.pos);
    surf.normal = CalculateWorldNormal(pin.normal, pin.tangent, pin.texcoord, NormalMap, LinearSampler);

    float viewDepth = length(cameraPosition.xyz - pin.position.xyz);
    float shadowWeight = CalculateShadowWeight(pin.position, viewDepth);

    // 半球の環境光。上を向く面ほど空の色、下を向く面ほど地面の色（どちらも a が強さ）
    float hemiWeight = surf.normal.y * 0.5f + 0.5f;
    float3 ambient = lerp(gDirLight.gGroundColor.rgb * gDirLight.gGroundColor.a, gDirLight.gSkyColor.rgb * gDirLight.gSkyColor.a, hemiWeight);

    LightResult light = CalculateAllLights(surf, shadowWeight);
    float3 finalColor = surf.color * (light.diffuse + ambient) + light.specular;

    finalColor = ApplyHeightFog(surf.pos, cameraPosition.xyz, finalColor);
    finalColor = ApplyHitFlash(finalColor, flashIntensity);

    return float4(finalColor, surf.alpha);
}