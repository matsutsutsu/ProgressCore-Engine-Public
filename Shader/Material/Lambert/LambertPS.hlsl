// ===========================================================================
// 概要: Lambert マテリアル。拡散反射と半球ライティングで照らし、フォグを掛ける
//
// 入力: LambertVS の出力（UV・法線・ワールド座標）
// 出力: 色（α 付き）
//
// 備考: ほぼ透明なピクセルは捨てる
// ===========================================================================
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/Lighting.hlsli"

cbuffer CbMaterial : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 baseColor; // テクスチャに掛ける色
};

Texture2D AlbedoMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    // 処理フロー:
    // 1. 面の色を求め、ほぼ透明なら捨てる
    // 2. 面の情報をまとめる
    // 3. ライトと影を計算する
    // 4. 半球ライティングの環境光を足し、フォグを掛ける

    // ---------------------------------------------------------
    // 1. 面の色
    // ---------------------------------------------------------

    float4 texColor = AlbedoMap.Sample(LinearSampler, pin.texcoord);
    float4 surfaceColor = texColor * baseColor;

    if (surfaceColor.a < 0.01f)
        discard;

    // ---------------------------------------------------------
    // 2. 面の情報
    // ---------------------------------------------------------

    SurfaceInfo surf;
    surf.pos = pin.position.xyz;
    surf.normal = normalize(pin.normal);
    surf.viewDir = normalize(cameraPosition.xyz - pin.position.xyz);
    surf.color = surfaceColor.rgb;
    surf.alpha = surfaceColor.a;

    // ---------------------------------------------------------
    // 3. ライトと影
    // ---------------------------------------------------------

    float viewDepth = length(cameraPosition.xyz - pin.position.xyz);
    float shadowWeight = CalculateShadowWeight(pin.position, viewDepth);

    LightResult lit = CalculateAllLights(surf, shadowWeight);

    // ---------------------------------------------------------
    // 4. 環境光とフォグ
    // ---------------------------------------------------------

    // 空の色と地面の色で、影の中も真っ暗にならないようにする
    float3 ambient = CalcHemiSphereLight(surf.normal, float3(0, 1, 0),
                                         gDirLight.gSkyColor.rgb, gDirLight.gGroundColor.rgb,
                                         float4(1, 1, 1, 1));

    float3 finalColor = lit.diffuse + lit.specular + (surfaceColor.rgb * ambient);

    finalColor = ApplyHeightFog(surf.pos, cameraPosition.xyz, finalColor);

    return float4(finalColor, surf.alpha);
}