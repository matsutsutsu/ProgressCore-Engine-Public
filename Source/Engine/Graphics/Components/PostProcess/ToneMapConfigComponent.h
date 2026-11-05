#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

// トーンマッピングの設定。シーンに1つ置き、PostProcessSystem が毎フレーム読む
// HDR を画面に出すのに必須なのでオン/オフは持たない。コンポーネントがなければ露出 1.0 で描く
struct ToneMapConfigComponent
{
    GpuData::CbToneMapData data; // 定数バッファの構造体をそのまま持つ
};
