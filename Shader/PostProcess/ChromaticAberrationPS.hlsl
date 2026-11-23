// ===========================================================================
// 概要: 色収差。赤と青を中心から逆向きにずらし、画面の縁に色のにじみを出す
//
// 入力: シーンの色
// 出力: 赤と青をずらしたシーンの色
//
// 備考: 緑はずらさない。中心（chromaCenter）から離れるほど大きくずれる
// ===========================================================================
#include "Shader/PostProcess/PostProcessVS.hlsli"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

Texture2D    gSceneTexture : register(REG_SRV(SLOT_SRV_POST_INPUT0));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

float4 main(VS_OUT input) : SV_TARGET
{
    float2 uv  = input.texcoord;
    float2 dir = uv - chromaCenter;
    float  distFromCenter = length(dir);

    // ずらす量。chromaFalloff が大きいほど、中心の近くはずれずに縁だけずれる
    float amount = pow(saturate(distFromCenter), chromaFalloff) * chromaStrength;
    float2 offset = dir * amount;

    float r = gSceneTexture.Sample(LinearSampler, saturate(uv + offset)).r;
    float g = gSceneTexture.Sample(LinearSampler, uv).g;
    float b = gSceneTexture.Sample(LinearSampler, saturate(uv - offset)).b;

    return float4(r, g, b, 1.0f);
}
