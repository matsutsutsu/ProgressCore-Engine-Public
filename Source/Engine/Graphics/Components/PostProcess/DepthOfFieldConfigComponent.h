#pragma once
// ===========================================================================
// 概要: 被写界深度（DoF）の設定を持つシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - DoF のオン/オフと GPU パラメータを持つ
//
// 備考: PostProcessSystem が毎フレーム読み、DepthOfFieldEffect へ渡す。既定はオフ。
//       data は定数バッファの構造体なので、シリアライズは Meta の手書き（パターンC）。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct DepthOfFieldConfigComponent
{
    bool enable = false;

    // ピント位置 10m / その前後 3m はぼけない / そこから最大のぼけまで 8m / 最大のぼけ半径 0.01（UV）
    GpuData::CbDepthOfFieldData data{10.0f, 3.0f, 8.0f, 0.01f};
};
