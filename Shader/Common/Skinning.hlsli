#ifndef SKINNING_HLSLI
#define SKINNING_HLSLI

// boneTransforms の cbuffer は ShaderShared.h で C++ と共有している
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

// 4本のボーンの行列を重みで混ぜて、位置を動かす
// ボーンの行列はオフセット行列 × ボーンのワールド行列なので、結果はワールド座標になる
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

// 法線や接線などの向きを動かす（w = 0 にして平行移動を効かせない）
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