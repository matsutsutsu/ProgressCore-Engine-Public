#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

// ブルームの設定。シーンに1つ置き、PostProcessSystem が毎フレーム読む
// コンポーネントがないか enable が false なら、ブルームは無効
struct BloomConfigComponent
{
    bool                 enable = true;
    GpuData::CbBloomData data; // 定数バッファの構造体をそのまま持つ
};
