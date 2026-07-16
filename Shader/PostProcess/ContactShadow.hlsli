#ifndef CONTACT_SHADOW_HLSLI
#define CONTACT_SHADOW_HLSLI

#include "Engine/Graphics/Shader/ShaderResources.h"

// PostProcessShared.h は b2 に複数 cbuffer がマージされるため、
// ContactShadow 専用シェーダーではこのファイルだけを include する。
cbuffer CbContactShadow : register(REG_CB(SLOT_CB_CONTACTSHADOW))
{
    float csRayLength;
    float csThickness;
    float csBias;
    float csIntensity;
    float csMaxDistance;
    float3 csPad;
};

#endif
