// Shader/Material/EnergyStream/EnergyStreamPS.hlsl
// Category : unlit
// PSO      : blend=additive  (shaders.json: { "name":"EnergyStream", "blend":"additive" })
// Params   : streamColor(f4) 色 / flowSpeed(f) 流速 / frequency(f) 縞の細かさ
//            intensity(f) 強さ / sharpness(f) 縞のコントラスト
// Textures : なし（完全プロシージャル。テクスチャ未設定でも必ず表示される）
// Use      : エネルギー帯・ビーム・オーラ・魔法陣の流れる光。UVのV方向へ流れる。
//
// gTime(CbScene.gTimeParams) を使って UV に沿って光の帯を流す。
// 2層の位相違いの縞を掛け合わせて、単調にならない有機的な流れを作る。
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"  // GetTime / ScrollUV

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 streamColor; // rgb: 帯の色
    float  flowSpeed;   // 流れる速さ（1〜3目安。負で逆流）
    float  frequency;   // 縞の細かさ（1で粗い、10〜30で細かい筋）
    float  intensity;   // 発光の強さ
    float  sharpness;   // 縞のコントラスト（1で柔らか、4〜8で鋭い筋に）
};

// 0..1 の帯パターンを生成（位相をずらした2層の合成）
float FlowBands(float2 uv, float t)
{
    // V方向に流す。X方向に少し傾けて斜めの流れにする。
    float phase1 = (uv.y * frequency - t * flowSpeed) + uv.x * 2.0f;
    float phase2 = (uv.y * frequency * 1.7f - t * flowSpeed * 1.3f) - uv.x * 1.5f;

    float band1 = 0.5f + 0.5f * sin(phase1 * 6.2831853f);
    float band2 = 0.5f + 0.5f * sin(phase2 * 6.2831853f);

    // 2層を掛けて「筋が交差して明滅する」有機的なパターンにする
    float bands = band1 * band2;

    // sharpness でコントラストを上げ、細い光の筋に絞り込む
    return pow(saturate(bands), max(sharpness, 0.0001f));
}

float4 main(VS_OUT pin) : SV_TARGET
{
    float t = GetTime();

    // ScrollUV も使い、全体を緩やかに流してムラを動かす
    float2 uv = ScrollUV(pin.texcoord, float2(0.0f, 0.05f) * flowSpeed);

    float bands = FlowBands(uv, t);

    float3 finalColor = streamColor.rgb * bands * intensity;

    // 加算合成なので出力アルファ=強さ倍率。帯の明るさをそのままアルファに使い、
    // 暗い部分がしっかり透けるようにする（帯の隙間が背景に溶ける）。
    return float4(finalColor, bands);
}
