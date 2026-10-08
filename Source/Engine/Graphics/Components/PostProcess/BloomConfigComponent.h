#pragma once
// ===========================================================================
// 概要: ブルームの設定を持つシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - ブルームのオン/オフと GPU パラメータを持つ
//
// 備考: PostProcessSystem が毎フレーム読み、BloomEffect へ渡す。
//       コンポーネントが無いか enable=false なら、ブルームは無効になる。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct BloomConfigComponent
{
    bool                 enable = true;
    GpuData::CbBloomData data; // 定数バッファの構造体をそのまま持つ
};
