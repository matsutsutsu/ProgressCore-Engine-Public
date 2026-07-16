#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct VignetteConfigComponent
{
    bool enable = true;

    GpuData::CbVignetteData data{
        { 0.0f, 0.0f, 0.0f, 1.0f },
        0.4f,
        0.55f,
        0.35f,
        0.0f,
        0.0f
    };
};
