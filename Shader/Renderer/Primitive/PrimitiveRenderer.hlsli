#ifndef PRIMITIVE_HLSLI
#define PRIMITIVE_HLSLI

// PrimitiveRenderer（箱・球・カプセルなどの単純な形）の VS と PS が共有する型
#include "Shader/Common/Scene.hlsli"
#include "Shader/Common/Instancing.hlsli"

// C++ の入力レイアウト（D3D11_INPUT_ELEMENT_DESC）と同じ並びにする
struct VS_IN
{
    float4 position : POSITION;
    float3 normal : NORMAL;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;        // インスタンスの色
    float3 worldNormal : NORMAL; // ワールド空間の法線
};

#endif