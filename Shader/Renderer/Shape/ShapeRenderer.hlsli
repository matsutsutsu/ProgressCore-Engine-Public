#ifndef SHAPE_HLSLI
#define SHAPE_HLSLI

struct VS_OUT
{
	float4 position : SV_POSITION;
	float4 color    : COLOR;
};

cbuffer SLOT_CB_MESH : register(b0)
{
	row_major float4x4	worldViewProjection;
	float4				color;
};

#endif