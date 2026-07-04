// Shader/Material/Fire/FirePS.hlsl
// Category : unlit
// PSO      : blend=additive  (shaders.json: { "name":"Fire", "blend":"additive" })
// Params   : coreColor(f4) 根本の熱い色 / tipColor(f4) 先端の冷めた色
//            flickerSpeed(f) 揺らめきの速さ / noiseScale(f) 乱流の細かさ
//            intensity(f) 強さ / heightFalloff(f) 上に行くほどの減衰
// Textures : なし（gGlobalNoiseMap を自動使用。テクスチャ未設定でも必ず表示される）
// Use      : 松明・焚き火・爆発の火柱など。UV.y=0が根本、1が先端の板ポリ前提。
//            （メッシュのUVが逆なら 1.0-pin.texcoord.y に読み替える）
//
// 速度・スケールの異なる2つのノイズサンプルを掛け合わせて、単調でない
// 揺らめきを作る（1層だけだと機械的な波にしか見えないため）。
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 coreColor;     // rgb: 根本の熱い色（白〜黄色が自然）
    float4 tipColor;      // rgb: 先端の冷めた色（赤〜暗い赤）
    float  flickerSpeed;  // 揺らめきの速さ（1〜3目安）
    float  noiseScale;    // 乱流の細かさ（3〜8目安）
    float  intensity;     // 発光の強さ
    float  heightFalloff; // 上に行くほど減衰する強さ（1〜3目安）
};

SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT pin) : SV_TARGET
{
    // 速度違いの2層を掛け合わせて有機的な乱流にする
    float n1 = SampleGlobalNoise(LinearSampler, pin.texcoord * noiseScale,
                                 float2(0.0f, -flickerSpeed));
    float n2 = SampleGlobalNoise(LinearSampler, pin.texcoord * noiseScale * 2.3f + 0.37f,
                                 float2(0.0f, -flickerSpeed * 1.6f));
    float turbulence = n1 * n2;

    // 先端に向かうほど炎が薄くなるマスク（UV.y=0:根本 → 1:先端 前提）
    float heightMask = saturate(1.0f - pin.texcoord.y * heightFalloff);
    float flame = turbulence * heightMask;

    // 炎が強いところほど熱い色(core)、弱いところほど冷めた色(tip)
    float3 col = lerp(tipColor.rgb, coreColor.rgb, saturate(flame * 1.6f));
    float3 finalColor = col * flame * intensity;

    // 加算合成: flame(0〜1)をそのままアルファにして、炎の隙間を背景に溶かす
    return float4(finalColor, flame);
}
