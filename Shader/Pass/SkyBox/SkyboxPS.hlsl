#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Shader/Common/Scene.hlsli" // CbFog(gFogColor) を取り込む

// IBLですでにセットされている s1 を流用する超絶スマートな設計
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

    // Mipレベル強制(SampleLevel)をやめ、通常のSampleを使用する
    // これによりカメラの解像度に合わせた最適なミップマップが自動選択され、
    // ジャギー（ピクセルのチラつき）のない最高品質の空が描画されます。
    float3 envColor = SkyboxMap.Sample(LinearSampler, dir).rgb;

    // 空の背景だけを暗くする。IBL（物の陰影）は別のテクスチャから引くので変わらない。
    // 地平線フォグより前に掛け、霧の色は FogComponent の色のまま保つ
    envColor *= skyIntensity;

    // ------------------------------------------------------------------
    // 距離フォグと背景を繋ぐ「地平線フォグ」。
    //   視線が地平線より下（島の下の虚空）を向くほど gFogColor へ寄せる。
    //   これで空っぽの空間も霧色になり、ApplyHeightFog で霧に溶けた地形と
    //   背景が同じ色で連続する（境目が消える）。
    //   dir.y : +1=真上, 0=地平線, -1=真下
    //   強度は gFogColor.a（FogComponent.color の α）と連動。
    //   位置/幅は FogComponent の skyFogHeight / skyFogSoftness で調整可能。
    //   フォグ無効/未設定時は gFogColor.a=0 なので下2行は無効化される。
    // ------------------------------------------------------------------
    float kHorizon = gSkyFogParams.x;              // フォグ開始の視線高
    float kBand    = max(gSkyFogParams.y, 1e-3f);  // フェード幅（0除算回避）
    float fogT = 1.0f - smoothstep(kHorizon, kHorizon + kBand, dir.y);
    fogT       = saturate(fogT * gFogColor.a);
    envColor   = lerp(envColor, gFogColor.rgb, fogT);

    return float4(envColor, 1.0f);
}
