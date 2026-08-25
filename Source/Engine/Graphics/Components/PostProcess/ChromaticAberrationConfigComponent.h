#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct ChromaticAberrationConfigComponent
{
    bool enable = true;

    // choromaCenter, chromaStrength, chromaFalloff
    GpuData::CbChromaticData data{ { 0.5f, 0.5f }, 0.0f, 2.0f };
};
