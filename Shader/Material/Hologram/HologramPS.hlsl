// Shader/Material/Hologram/HologramPS.hlsl
// Category : unlit
// PSO      : blend=additive  (shaders.json: { "name":"Hologram", "blend":"additive" })
// Params   : rimColor(f4) 縁色 / rimPower(f) 縁の鋭さ / intensity(f) 強さ
//            pulseSpeed(f) 脈動速度(0=無し) / scanlineDensity(f) 走査線の密度(0=無し)
//            scanlineStrength(f) 走査線の濃さ(0.2〜0.4推奨)
// Textures : なし
// Use      : シールド/ホログラム/幽霊の縁。殻メッシュや単純形状に適用推奨。
//
// 改良点: CbScene.gTimeParams(gTime) を使い、脈動(Pulse)と流れる走査線を追加。
//         リム計算は MaterialEffect.hlsli の ApplyRimLightParams に集約
//         （マテリアルごとに power/color/strength を制御できる版）。
#include "Shader/Common/Common.hlsli"
#include "Shader/Common/MaterialEffect.hlsli"

cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 rimColor;         // rgb: 縁の発光色
    float  rimPower;         // 縁の鋭さ（大きいほど薄く鋭い。目安 1〜8）
    float  intensity;        // 発光の強さ
    float  pulseSpeed;       // 脈動の速さ（0で脈動なし。1.0で穏やかな明滅）
    float  scanlineDensity;  // 走査線の密度（0で走査線なし）
    float  scanlineStrength; // 走査線の濃さ（0.2〜0.4が自然。1.0だと強い縞）
    float3 _pad;             // 16byte境界合わせ
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 normal  = normalize(pin.normal);
    float3 viewDir = normalize(cameraPosition.xyz - pin.position.xyz);

    // 1. フレネル縁発光（黒ベースに加算する形で強度を得る）
    float3 rim = ApplyRimLightParams(float3(0, 0, 0), normal, viewDir,
                                     rimPower, rimColor.rgb, 1.0f);

    // 2. 脈動（pulseSpeed=0 なら等倍のまま）
    float pulse = (pulseSpeed > 0.0f) ? Pulse(0.55f, 1.0f, pulseSpeed) : 1.0f;

    // 3. 流れる走査線（scanlineDensity=0 なら無効。時間で下から上へ流れる）
    float scan = 1.0f;
    if (scanlineDensity > 0.0f)
    {
        float band = 0.5f + 0.5f * sin((pin.texcoord.y * scanlineDensity - GetTime() * 2.0f) * 6.2831853f);
        scan = 1.0f - scanlineStrength * (1.0f - band); // band=1→減衰なし, band=0→strength分暗く
    }

    float3 finalColor = rim * intensity * pulse * scan;
    return float4(finalColor, 1.0f);
}
