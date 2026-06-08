#ifndef SKINNING_HLSLI
#define SKINNING_HLSLI

// 定数バッファは ShaderShared.h に移動（C++と共有）
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

float4 SkinningPosition(float4 position, float4 boneWeights, uint4 boneIndices)
{
	float4 p = float4(0, 0, 0, 0);

	[unroll]
	for (int i = 0; i < 4; i++)
	{
		p += (boneWeights[i] * mul(position, boneTransforms[boneIndices[i]]));
	}
	return p;
}

float3 SkinningVector(float3 vec, float4 boneWeights, uint4 boneIndices)
{
	float3 v = float3(0, 0, 0);

	[unroll]
	for (int i = 0; i < 4; i++)
	{
		v += boneWeights[i] * mul(float4(vec, 0), boneTransforms[boneIndices[i]]).xyz;
	}
	return v;
}

#endif // SKINNING_HLSLI