#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct ColorGradingConfigComponent
{
    bool enable = true;

    GpuData::CbColorGradingData data{
        { 1.0f, 1.0f, 1.0f, 1.0f },
        1.0f, 1.0f, 1.0f, 1.0f
    };
};
