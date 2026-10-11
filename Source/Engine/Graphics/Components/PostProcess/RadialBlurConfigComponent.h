#pragma once
// ===========================================================================
// 概要: 放射状ブラー（集中線）の設定と、演出からの一時的な上乗せを持つ
//       シングルトンコンポーネント。
//
// 責務:
//   - シーンが持つ普段の設定（enable / data: 中心・強さ・立ち上がり）を持つ
//   - 演出ごとの一時的な上乗せ（overrides）を持つ
//   - 両方を合わせた最終的な強さを求める（ResolvedStrength）
//   - 上書きの土台になる、今効いている強さを返す（BaseParams）
//
// 備考: 演出システムは enable / data を書き換えず、自分のスロットだけを触る。
//       スロットの仕組みと理由は PostEffectOverrides.h を参照。
//       PostProcessSystem が毎フレーム読み、RadialBlurEffect へ渡す。
//       data は定数バッファの構造体なので、シリアライズは Meta の手書き（パターンC）。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <algorithm>

// 放射状ブラーの一時上書きの中身
struct RadialBlurOverrideParams
{
    float strength = 0.0f; // 演出が出したい強さのピーク
};

struct RadialBlurConfigComponent
{
    bool enable = false;

    // 中心（UV）/ 強さ / 中心からの立ち上がりの鋭さ
    GpuData::CbRadialBlurData data{{0.5f, 0.5f}, 0.0f, 2.0f};

    PostEffectOverrides<RadialBlurOverrideParams> overrides;

    // 描画に渡す強さ。普段の値と、効いている全スロットの最大値をとる（色収差と同じ理由）
    float ResolvedStrength() const
    {
        float out = enable ? data.blurStrength : 0.0f;
        overrides.ForEachActive([&out](const RadialBlurOverrideParams &p, float t) {
            out = (std::max)(out, p.strength * t);
        });
        return out;
    }

    // 上書きの土台になる、今効いている強さ。enable=false なら 0（理由は色収差と同じ）
    RadialBlurOverrideParams BaseParams() const
    {
        RadialBlurOverrideParams p;
        p.strength = enable ? data.blurStrength : 0.0f;
        return p;
    }
};
