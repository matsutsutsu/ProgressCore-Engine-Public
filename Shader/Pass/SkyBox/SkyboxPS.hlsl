// ===========================================================================
// 概要: 空（SkyboxPass）。視線の向きでキューブマップを読み、地平線より下をフォグの色に寄せる
//
// 入力: SkyboxVS の出力（視線の向き）
// 出力: 空の色（HDR）
//
// 備考: 地平線のフォグで、ApplyHeightFog で霧に溶けた地形と、その奥の空を同じ色でつなぐ
//       フォグがないシーンは gFogColor.a が 0 なので、空の色のまま
// ===========================================================================
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Shader/Common/Scene.hlsli"

TextureCube SkyboxMap : register(REG_SRV(SLOT_SRV_SKYBOX_BG));
SamplerState LinearSampler : register(REG_SMP(SLOT_SMP_LINEAR));

// SkyboxPass.h の CbSkybox・SkyboxVS.hlsl と同じ並び
cbuffer CbSkybox : register(REG_CB(SLOT_CB_SKYBOX))
{
    row_major float4x4 invViewProj; // VS で使う
    float skyIntensity;             // 空の明るさの倍率（SkyboxComponent の値。無ければ 1.0）
    float3 skyPadding;
};

struct VS_OUT
{
    float4 pos : SV_POSITION;
    float3 rayDir : TEXCOORD;
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 dir = normalize(pin.rayDir);

    // ミップは指定せず、画面の大きさに合ったものを自動で選ばせる（指定するとちらつく）
    float3 envColor = SkyboxMap.Sample(LinearSampler, dir).rgb;

    // 空の背景だけを暗くする。IBL（物の陰影）は別のテクスチャから引くので変わらない
    // 地平線のフォグより前に掛け、霧の色は FogComponent の色のまま保つ
    envColor *= skyIntensity;

    // 地平線のフォグ。視線が gSkyFogParams.x の高さより下を向くほど、gFogColor へ寄せる
    // dir.y は +1 で真上、0 で地平線、-1 で真下。強さは gFogColor.a
    float kHorizon = gSkyFogParams.x;              // フォグがかかり始める視線の高さ
    float kBand    = max(gSkyFogParams.y, 1e-3f);  // ぼかす幅（0 で割らないよう下限を付ける）
    float fogT = 1.0f - smoothstep(kHorizon, kHorizon + kBand, dir.y);
    fogT       = saturate(fogT * gFogColor.a);
    envColor   = lerp(envColor, gFogColor.rgb, fogT);

    return float4(envColor, 1.0f);
}
