// ===========================================================================
// 概要: 単純な形（PrimitiveRenderer）のピクセルシェーダー。平行光源だけで簡単に陰を付ける
//
// 入力: PrimitiveRendererVS の出力（色・ワールド空間の法線）
// 出力: 色（α は元の色のまま）
// ===========================================================================
#include "Shader/Renderer/Primitive/PrimitiveRenderer.hlsli"
#include "Shader/Common/Lighting.hlsli"

float4 main(VS_OUT pin) : SV_TARGET
{
    // 補間で長さが 1 からずれるので、正規化し直す
    float3 N = normalize(pin.worldNormal);

    // gDirLight.direction は光が進む向きなので、逆にして面からライトへの向きにする
    float3 L = normalize(-gDirLight.direction);

    float NdotL = saturate(dot(N, L));

    // 光の当たらない側が真っ黒になって形が読めなくならないよう、最低限の明るさを足す
    float ambient = 0.3f;

    float3 lightFinalColor = gDirLight.color * gDirLight.intensity;

    float3 finalColor = pin.color.rgb * (NdotL + ambient) * lightFinalColor;

    // α は元の色のまま（半透明の形を描くため）
    return float4(finalColor, pin.color.a);
}