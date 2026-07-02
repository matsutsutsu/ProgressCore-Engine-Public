// Shader/Material/Hologram/HologramPS.hlsl
//
// フレネル発光(Hologram)マテリアル用のピクセルシェーダー。
// 視線ベクトルと法線の角度(dot積)から輪郭部分だけを光らせる Unlit シェーダー。
// Glow(全面が均一に光る)との違いは、この視線依存の項(fresnel)があること。
// カメラが動く/オブジェクトが回転すると光る位置がリアルタイムに変わる。
//
// cameraPosition は CbScene(Per-frame, b8)由来。Common.hlsli経由で
// Scene.hlsli が既にincludeされているため、マテリアル側で追加のバインドは不要
// （Graphics::Update が毎フレーム自動で埋める）。
#include "Shader/Common/Common.hlsli"

// ==============================================================================
// 定数バッファ
//   ★ 変数名がそのまま MaterialData の nameHash キーになる
// ==============================================================================
cbuffer CbMesh : register(REG_CB(SLOT_CB_MATERIAL))
{
    float4 rimColor;   // rgb: 縁の発光色
    float  rimPower;   // 縁の鋭さ（大きいほど薄く鋭い輪郭になる。目安 1〜8）
    float  intensity;  // 発光の強さ（1.0が基準）
    float2 _pad;       // 16byte境界合わせ
};

float4 main(VS_OUT pin) : SV_TARGET
{
    float3 normal  = normalize(pin.normal);
    float3 viewDir = normalize(cameraPosition.xyz - pin.position.xyz);

    // 正面(dot≈1)ではfresnel≈0、輪郭(dot≈0)ではfresnel≈1 になる。
    // rimPower を上げるほど、輪郭付近だけに絞り込まれた鋭いカーブになる。
    float NdotV    = saturate(dot(normal, viewDir));
    float fresnel  = pow(1.0f - NdotV, rimPower);

    float3 finalColor = rimColor.rgb * fresnel * intensity;

    // ★ 加算合成(BlendState::Additive)は SrcBlend=SRC_ALPHA。
    //   色側で既に fresnel を掛けて減衰させているので、アルファは1.0のままで良い
    //   （Glowのようにテクスチャのアルファをマスクとして使う必要が無いケース）。
    return float4(finalColor, 1.0f);
}
