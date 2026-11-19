// ===========================================================================
// 概要: マテリアルの見た目に足す小さな演出の関数（白い点滅・UV スクロール・脈打ち・リムライト・揺らぎ）
//
// 備考: 時間は CbScene の gTimeParams から読む（x = 経過秒、y = 前のフレームからの秒）
// ===========================================================================
#ifndef MATERIAL_EFFECT_HLSLI
#define MATERIAL_EFFECT_HLSLI

#include "Shader/Common/Scene.hlsli"

// 被弾したときの白い点滅。flashIntensity が 1 で真っ白
float3 ApplyHitFlash(float3 baseColor, float flashIntensity)
{
    return lerp(baseColor, float3(1.0, 1.0, 1.0), flashIntensity);
}

float GetTime()      { return gTimeParams.x; }
float GetDeltaTime() { return gTimeParams.y; }

// UV を時間で流す（流れる炎・水・光の帯など）
float2 ScrollUV(float2 uv, float2 speed)
{
    return uv + speed * GetTime();
}

// minV と maxV の間を、1秒に speed 回往復する値（明滅・鼓動など）
float Pulse(float minV, float maxV, float speed)
{
    float t = 0.5f + 0.5f * sin(GetTime() * speed * 6.2831853f);
    return lerp(minV, maxV, t);
}

// リムライト。視線と面が平行に近い輪郭ほど color を足す。power が大きいほど輪郭の細い所だけ光る
float3 ApplyRimLightParams(float3 baseColor, float3 normal, float3 viewDir,
                           float power, float3 color, float strength)
{
    if (strength <= 0.0f)
        return baseColor;

    float NdotV = saturate(dot(normal, viewDir));
    float rim   = pow(1.0f - NdotV, power);
    return baseColor + rim * color * strength;
}

// 共通のノイズテクスチャを流して読む（炎や水面の揺らぎ）
// ノイズは EnvironmentSystem が毎フレーム置くので、マテリアルにテクスチャを設定しなくてよい
float SampleGlobalNoise(SamplerState smp, float2 uv, float2 scrollSpeed)
{
    float2 scrolledUV = uv + scrollSpeed * GetTime();
    return gGlobalNoiseMap.Sample(smp, scrolledUV).r;
}

#endif // MATERIAL_EFFECT_HLSLI
