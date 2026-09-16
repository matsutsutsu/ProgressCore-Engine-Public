#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct DepthOfFieldConfigComponent
{
    bool enable = false;

    GpuData::CbDepthOfFieldData data{10.0f, 3.0f, 8.0f, 0.01f};
};
