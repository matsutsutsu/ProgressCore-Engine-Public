#include "Shader/PostProcess/Bloom.hlsli"

cbuffer CbToneMap : register(REG_CB(SLOT_CB_TONEMAP)) {
    float gExposure;
    float gChromaticAberration;
    float2 ggPadding;
    float4 gColorTint;
};

float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv = input.texcoord;

    // 画面中心 (0.5, 0.5) を原点としたベクトルを計算
    // 画面の端に行くほど歪みが強くなるレンズの物理挙動をシミュレートする
    float2 centerDir = uv - 0.5f;

    // 【1】色収差 (Chromatic Aberration) のサンプリング
    // R, G, B で別々のUV座標を計算してサンプリングする
    float2 uvR = uv - centerDir * gChromaticAberration;
    float2 uvG = uv; // 緑は人間の目の感度が高いため基準とする
    float2 uvB = uv + centerDir * gChromaticAberration;

    float r = gInputTexture.Sample(LinearSampler, uvR).r;
    float g = gInputTexture.Sample(LinearSampler, uvG).g;
    float b = gInputTexture.Sample(LinearSampler, uvB).b;

    float3 hdrColor = float3(r, g, b);

    // 【2】動的カラーグレーディング（露出 ＆ ティント）
    // ヒットストップ中に colorTint を (1.5, 0.8, 0.8) などにすれば、血走ったような緊迫感が出る
    hdrColor *= gExposure * gColorTint.rgb;

    // 【3】トーンマッピング (HDR -> SDR)
    float3 sdrColor = ACESFilm(hdrColor);
    
    // 【4】ガンマ補正 (Linear -> sRGB)
    // お前のバックバッファフォーマット(UNORM)に合わせて、モニター出力用の補正を行う
    sdrColor = pow(sdrColor, float3(0.4545f, 0.4545f, 0.4545f));

    return float4(sdrColor, 1.0f);
}