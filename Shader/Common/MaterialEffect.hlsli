// Shader/Common/MaterialEffect.hlsli
#ifndef MATERIAL_EFFECT_HLSLI
#define MATERIAL_EFFECT_HLSLI

#include "Shader/Common/Scene.hlsli"

// ==========================================================
// 1. ヒットフラッシュ (被ダメージ時の白点滅)
// ==========================================================
float3 ApplyHitFlash(float3 baseColor, float flashIntensity)
{
    return lerp(baseColor, float3(1.0, 1.0, 1.0), flashIntensity);
}

// ==========================================================
// 2. 時間ユーティリティ（CbScene.gTimeParams 由来）
//   Scene.hlsli 経由で gTimeParams が見えている前提。
//   x:経過秒 y:デルタ秒 z:sin(経過秒)
// ==========================================================
float GetTime()      { return gTimeParams.x; }
float GetDeltaTime() { return gTimeParams.y; }

// UVスクロール（流れる炎・水・エネルギー帯など）
float2 ScrollUV(float2 uv, float2 speed)
{
    return uv + speed * GetTime();
}

// 脈動（0..1 を min..max の範囲で行き来させる。明滅・鼓動表現に）
//   speed: 1秒あたりの往復回数の目安
float Pulse(float minV, float maxV, float speed)
{
    float t = 0.5f + 0.5f * sin(GetTime() * speed * 6.2831853f);
    return lerp(minV, maxV, t);
}

// ==========================================================
// 3. リムライト（輪郭発光）
//   power/color/strength を引数で受け取るので、呼ぶ側が値の出どころを決められる。
// ==========================================================
float3 ApplyRimLightParams(float3 baseColor, float3 normal, float3 viewDir,
                           float power, float3 color, float strength)
{
    if (strength <= 0.0f)
        return baseColor;

    float NdotV = saturate(dot(normal, viewDir));
    float rim   = pow(1.0f - NdotV, power);
    return baseColor + rim * color * strength;
}

// ==========================================================
// 4. グローバルノイズを使った乱流サンプリング
//   gGlobalNoiseMap (t13, Scene.hlsli で宣言) は EnvironmentSystem が
//   毎フレーム無条件にバインドするため、マテリアル側でのテクスチャ設定が
//   一切不要。炎や水面のような「揺らめき」表現の乱流源として使う。
// ==========================================================
float SampleGlobalNoise(SamplerState smp, float2 uv, float2 scrollSpeed)
{
    float2 scrolledUV = uv + scrollSpeed * GetTime();
    return gGlobalNoiseMap.Sample(smp, scrolledUV).r;
}

#endif // MATERIAL_EFFECT_HLSLI
