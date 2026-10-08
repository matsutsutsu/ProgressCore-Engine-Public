#pragma once
// ===========================================================================
// 概要: 画面の歪みエフェクトの設定を持つシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - 歪みのオン/オフと GPU パラメータを持つ
//
// 備考: PostProcessSystem が毎フレーム data.time を進め、DistortionEffect へ渡す。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct DistortionConfigComponent
{
    bool                      enable = true;
    GpuData::CbDistortionData data; // 定数バッファの構造体をそのまま持つ
};
