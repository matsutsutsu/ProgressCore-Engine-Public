#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct BloomConfigComponent {
  bool                 enable = true;
  GpuData::CbBloomData data; // 構造体をそのまま内包
};