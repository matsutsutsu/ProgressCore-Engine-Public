#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct DistortionConfigComponent
{
    bool                      enable = true;
    GpuData::CbDistortionData data;
};