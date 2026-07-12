#ifndef SSAO_HLSLI
#define SSAO_HLSLI

#include "Engine/Graphics/Shader/ShaderResources.h"

// PostProcessShared.h は b2 に複数 cbuffer がマージされるため、
// SSAO 専用シェーダーではこのファイルだけを include する。
cbuffer CbSSAO : register(REG_CB(SLOT_CB_SSAO))
{
    float ssaoRadius;
    float ssaoIntensity;
    float ssaoBias;
    float ssaoMaxDistance;
};

#endif
