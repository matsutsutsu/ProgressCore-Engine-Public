#include "Shader/PostProcess/Bloom.hlsli"


// シーン画像(HDR)
Texture2D gSceneTexture : register(REG_SRV(SLOT_SRV_MAT_TEX0));
// ブルーム画像
Texture2D gBloomTexture : register(REG_SRV(SLOT_SRV_MAT_TEX1));

// トーンマップ設定
cbuffer CbToneMap : register(REG_CB(SLOT_CB_TONEMAP))
{
    float gExposure; // 露出 (1.0)
    float3 ggPadding;
};

// ACESフィルム調トーンマップ（映画のような色合いになる）
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
    // 1. シーンとブルームをサンプリング
    float3 sceneColor = gSceneTexture.Sample(LinearSampler, input.texcoord).rgb;
    float3 bloomColor = gBloomTexture.Sample(LinearSampler, input.texcoord).rgb;

    // 2. 加算合成 (Bloomを足す)
    float3 color = sceneColor + bloomColor;

    // 3. 露出調整
    color *= gExposure;

    // 4. トーンマッピング (HDR -> SDR)
    // 1.0を超える輝度を、0.0~1.0の範囲に綺麗に収める
    color = ACESFilm(color);

    // 5. ガンマ補正 (リニア -> sRGB)
    // モニターで正しく表示するために明るさを持ち上げる
    const float GAMMA = 2.2;
    color = pow(color, 1.0 / GAMMA);

    return float4(color, 1.0f);
}