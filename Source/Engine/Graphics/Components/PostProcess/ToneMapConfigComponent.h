#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct ToneMapConfigComponent
{
    bool                   enable = true; // 一貫性のためにenableを追加
    GpuData::CbToneMapData data;          // 構造体をそのまま内包
};