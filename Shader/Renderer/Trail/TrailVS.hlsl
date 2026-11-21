// ===========================================================================
// 概要: トレイルの頂点シェーダー。ワールド座標の頂点を画面へ移す（加算と歪みの層で共用）
//
// 入力: ワールド座標の頂点（位置・色・UV）
// 出力: 画面の位置・色・ワールド座標・UV
//
// 備考: ワールド座標を渡すのは歪みの層のため
//       PS で ddx / ddy を取ると面の法線が求まるので、頂点に法線を足さずに済む
//       加算の層（TrailPS）はワールド座標を読まない
// ===========================================================================
#include "Engine/Graphics/Shader/ShaderResources.h"

cbuffer CbTrail : register(REG_CB(SLOT_CB_TRAIL))
{
    row_major float4x4 viewProjection; // C++ が転置せずに送るので row_major
};

struct VSInput
{
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float3 worldPos : TEXCOORD0;
    float2 uv       : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), viewProjection);
    output.color    = input.color;
    output.worldPos = input.position;
    output.uv       = input.uv;
    return output;
}
