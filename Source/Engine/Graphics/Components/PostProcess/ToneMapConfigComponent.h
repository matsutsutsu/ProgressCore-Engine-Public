#pragma once
// ===========================================================================
// 概要: トーンマッピングの設定を持つシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - 露出・色収差・色の補正を GPU パラメータとして持つ
//
// 備考: トーンマッピングは HDR を画面に出すのに必須なので、オン/オフは持たない。
//       PostProcessSystem が毎フレーム読み、PostProcessManager へ渡す。
//       コンポーネントが無ければ露出 1.0 で描く。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct ToneMapConfigComponent
{
    GpuData::CbToneMapData data; // 定数バッファの構造体をそのまま持つ
};
