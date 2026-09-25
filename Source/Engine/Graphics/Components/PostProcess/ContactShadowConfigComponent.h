#pragma once
// ===========================================================================
// 概要: コンタクトシャドウの設定を持つシングルトンコンポーネント。シーンに1つ置く。
//
// 責務:
//   - コンタクトシャドウのオン/オフと GPU パラメータ
//     （レイの長さ・厚み・バイアス・強さ・効かせる最大距離）を持つ
//
// 備考: PostProcessSystem が毎フレーム読み、ContactShadowEffect へ渡す。
//       SSAO と同じく演出ではなく絵の基本品質のためのものなので、既定でオンにして薄く効かせる。
//       data は定数バッファの構造体なので、シリアライズは Meta の手書き（パターンC）。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"

struct ContactShadowConfigComponent
{
    bool enable = true;

    // レイの長さ 0.3m / 厚み 0.1m / バイアス 0.02 / 強さ 0.7 / 最大距離 30m
    GpuData::CbContactShadowData data{0.3f, 0.1f, 0.02f, 0.7f, 30.0f, {}};
};
