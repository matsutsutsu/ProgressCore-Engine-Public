// Shader/Material/FogVolume/FogVolumePS.hlsl
// Category : unlit
// PSO      : blend=transparency, depth=testOnly, cull=none
//            (shaders.json: { "name":"FogVolume", "blend":"transparency", "depth":"testOnly", "cull":"none" })
// Params   : fogColor(f4) 靄色+マスターα / density(f) 全体濃さ
//            noiseScale(f) ワールドXZノイズ尺度 / noiseStrength(f) ノイズによる濃淡(0=均一)
//            edgeSoftness(f) 板の端を見たときのフェード(大=ソフト) / useDiffuseMap(f) UVマスク
//            scrollSpeed(f2) ノイズの流れ
// Textures : DiffuseMap(t0) 任意（雲の形マスク。未使用時は useDiffuseMap=0）
// Use      : 浮島ステージの「空白を埋める靄」用水平板・囲い板。ApplyHeightFog は使わない。
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 fogColor;
    float  density;
    float  noiseScale;
    float  noiseStrength;
    float  edgeSoftness;
    float  useDiffuseMap;
    float2 scrollSpeed;
};

Texture2D DiffuseMap : register(REG_SRV(SLOT_SRV_MAT_TEX0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 normal  = normalize(pin.normal);
    float3 viewDir = normalize(cameraPosition.xyz - pin.position.xyz);

    // 水平板など：真上/真下から見るほど濃く、水平に見るほど薄く（切り口を隠す）
    float NdotV = saturate(abs(dot(normal, viewDir)));
    float edge  = pow(NdotV, max(edgeSoftness, 0.05f));

    // ワールドXZでノイズ（gGlobalNoiseMap / テクスチャ設定不要）
    float2 worldUV = pin.position.xz * max(noiseScale, 0.001f);
    float  n0      = SampleGlobalNoise(LinearSampler, worldUV, scrollSpeed);
    float  n1      = SampleGlobalNoise(LinearSampler, worldUV * 1.7f + float2(17.3f, 9.1f), scrollSpeed * 0.7f);
    float  noise   = lerp(1.0f, saturate(n0 * 0.65f + n1 * 0.35f), saturate(noiseStrength));

    float4 texMask = (useDiffuseMap > 0.5f)
        ? DiffuseMap.Sample(LinearSampler, pin.texcoord)
        : float4(1.0f, 1.0f, 1.0f, 1.0f);

    float alpha = fogColor.a * density * edge * noise * texMask.a;
    alpha       = saturate(alpha);

    return float4(fogColor.rgb, alpha);
}
