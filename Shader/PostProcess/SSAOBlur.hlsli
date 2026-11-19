#ifndef SSAO_BLUR_HLSLI
#define SSAO_BLUR_HLSLI

#include "Engine/Graphics/Shader/ShaderResources.h"

// SSAO のぼかしの設定。PostProcessShared.h の CbSSAOBlurData と同じ並びにする（変えるときは両方を直す）
cbuffer CbSSAOBlur : register(REG_CB(SLOT_CB_SSAO_BLUR))
{
    float2 blurTexelStep;   // 1点ずらす量（UV）。横の回は (1/幅, 0)、縦の回は (0, 1/高さ)
    float  blurDepthSigma;  // 深度の差に対する重みの広がり（m）。小さいほど物の境目でぼかさない
    float  blurMaxDistance; // これより遠いピクセルはぼかさない（m）
};

#endif
