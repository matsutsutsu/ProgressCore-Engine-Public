#ifndef SSAO_BLUR_HLSLI
#define SSAO_BLUR_HLSLI

#include "Engine/Graphics/Shader/ShaderResources.h"

cbuffer CbSSAOBlur : register(REG_CB(SLOT_CB_SSAO_BLUR))
{
    float2 blurTexelStep;
    float  blurDepthSigma;
    float  blurMaxDistance;
};

#endif
