// ===========================================================================
// 概要: PBR マテリアル。直接光（Cook-Torrance）と環境光（IBL）で照らし、発光とフォグを足す
//
// 入力: PBRVS の出力（UV・法線・接線・ワールド座標）
// 出力: 色（HDR、α 付き）
//
// 備考: 各テクスチャは use〜Map が 0.5 より大きいときだけ読む。ないときは白（法線は頂点の法線）として扱う
//       メタル・ラフネスのテクスチャは glTF の並び（R = AO、G = 粗さ、B = 金属らしさ）
// ===========================================================================
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/PBRLighting.hlsli"

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 baseColor;
    float4 emissiveColor; // rgb だけ使う。C++ は色を float4 で送るので、float3 にすると大きさが合わず届かない
    float metalness;         // 金属らしさ（テクスチャの B に掛ける）
    float roughness;         // 粗さ（テクスチャの G に掛ける）
    float occlusionStrength; // AO を効かせる割合（0 で効かせない）

    // テクスチャがあるか（1 / 0）。C++ がマテリアルの設定から送る
    float useAlbedoMap;
    float useNormalMap;
    float useMetalRoughMap;
    float useOcclusionMap;
    float useEmissiveMap;

    float alphaCutoff; // これより小さいアルファを捨てる。alphaMode が Mask のときだけ 0 より大きい（Shader が自動で送る）
};

Texture2D AlbedoMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
Texture2D NormalMap : register(REG_SRV(SLOT_SRV_MAT_TEX1));
Texture2D MetalRoughMap : register(REG_SRV(SLOT_SRV_MAT_TEX2));
Texture2D EmissiveMap : register(REG_SRV(SLOT_SRV_MAT_TEX3));

SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    // 処理フロー:
    // 1. テクスチャを読み、面の情報（法線・色・粗さなど）を決める
    // 2. 直接光と環境光を計算する
    // 3. 発光を足し、フォグを掛ける


    PBRSurfaceInfo surf;
    surf.pos = pin.position.xyz;
    surf.viewDir = normalize(cameraPosition.xyz - surf.pos);

    // ---------------------------------------------------------
    // 1. 面の情報
    // ---------------------------------------------------------

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

    // くり抜き（Mask）。Mask 以外は alphaCutoff が 0 なので、何も捨てない
    if (alpha < alphaCutoff)
        discard;

    float4 mrSample = useMetalRoughMap > 0.5f ? MetalRoughMap.Sample(LinearSampler, pin.texcoord) : float4(1.0f, 1.0f, 1.0f, 1.0f);

    surf.metalness = metalness * mrSample.b;
    surf.roughness = roughness * mrSample.g;
    float rawAO = useOcclusionMap > 0.5f ? mrSample.r : 1.0f;
    surf.ao = lerp(1.0f, rawAO, occlusionStrength);

    // 真正面の反射率。金属でない物はどれもほぼ 4%、金属は自分の色で反射する
    surf.f0 = lerp(float3(0.04f, 0.04f, 0.04f), surf.albedo, surf.metalness);

    // ---------------------------------------------------------
    // 2. 直接光と環境光
    // ---------------------------------------------------------

    float viewDepth = length(cameraPosition.xyz - pin.position.xyz);
    float shadowWeight = CalculateShadowWeight(pin.position, viewDepth);

    LightResult light = CalculateAllLightsPBR(surf, shadowWeight);

    float3 ambientIBL = CalculateIBL(surf);

    // 影の中は環境光を 20% まで落とす。IBL は影を知らないので、そのままだと影が薄く見える
    float skyOcclusion = lerp(0.2f, 1.0f, shadowWeight);
    ambientIBL *= skyOcclusion;

    // ---------------------------------------------------------
    // 3. 発光とフォグ
    // ---------------------------------------------------------

    float3 emissive = useEmissiveMap > 0.5f ? EmissiveMap.Sample(LinearSampler, pin.texcoord).rgb * emissiveColor.rgb : float3(0.0f, 0.0f, 0.0f);

    float3 finalColor = ambientIBL + light.diffuse + light.specular + emissive;

    finalColor = ApplyHeightFog(surf.pos, cameraPosition.xyz, finalColor);


    return float4(finalColor, alpha);
}