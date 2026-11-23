#ifndef SHAPE_HLSLI
#define SHAPE_HLSLI

// ShapeRenderer（デバッグ用の線）の VS と PS が共有する型
#include "Shader/Common/Scene.hlsli"
#include "Shader/Common/Instancing.hlsli"

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

#endif