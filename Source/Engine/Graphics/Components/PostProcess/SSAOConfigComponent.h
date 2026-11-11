#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

// SSAO の設定。シーンに1つ置き、PostProcessSystem が毎フレーム読む
// 絵の基本品質のためのものなので、既定でオンにして控えめに効かせる。data は定数バッファなので Meta は手書き
struct SSAOConfigComponent
{
    bool enable = true;

    // 半径 0.5m / 強さ 0.8 / バイアス 0.02 / 最大距離 50m
    GpuData::CbSSAOData data{0.5f, 0.8f, 0.02f, 50.0f};
};
