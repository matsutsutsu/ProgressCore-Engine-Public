#ifndef COMMON_HLSLI
#define COMMON_HLSLI

#include "Shader/Common/Scene.hlsli"
#include "Shader/Common/Lighting.hlsli"
#include "Shader/Pass/ShadowMap/ShadowMap.hlsli"


// プロジェクトで使う「最大公約数」的な頂点出力構造体
struct VS_OUT
{
    float4 vertex : SV_POSITION;
    float2 texcoord : TEXCOORD;
    float3 normal : NORMAL;
    float4 position : POSITION;
    float4 tangent : TANGENT;
    float4 shadowPos : SHADOW_POS;

    // 個体ごとの汎用パラメータを受け渡す
    float4 customParams : CUSTOM_PARAMS;
};


// 距離フォグと高さフォグを掛け合わせて、baseColor をフォグの色に寄せる（マテリアルのピクセルシェーダーから呼ぶ）
float3 ApplyHeightFog(float3 pixelPos, float3 camPos, float3 baseColor)
{
    // 1. 距離フォグ
    float dist = length(pixelPos - gFogCenter.xyz);
    float distFog = saturate((dist - gFogParams.x) / (gFogParams.y - gFogParams.x));

    // 2. 高さフォグ
    float heightFog = exp(-(pixelPos.y - gFogParams.z) * gFogParams.w);
    heightFog = saturate(heightFog);

    // 遠くて低い所ほど濃くなるよう、2つを掛け合わせる
    float fogDensity = distFog * heightFog;

    return lerp(baseColor, gFogColor.rgb, fogDensity * gFogColor.a);
}

#endif // COMMON_HLSLI