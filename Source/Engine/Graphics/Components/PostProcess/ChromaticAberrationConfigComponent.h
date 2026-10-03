#pragma once
// ===========================================================================
// 概要: 色収差の設定と、演出からの一時的な上乗せを持つシングルトンコンポーネント。
//
// 責務:
//   - シーンが持つ普段の設定（enable / data）を持つ
//   - 演出ごとの一時的な上乗せ（overrides）を持つ
//   - 両方を合わせた最終的な強さを求める（ResolvedStrength）
//
// 備考: 演出システムは enable / data を書き換えず、自分のスロットだけを触る。
//       スロットの仕組みと理由は PostEffectOverrides.h を参照。
//       enable=false でも演出中は描くので、PostProcessSystem は合成後の強さが 0 かどうかで
//       パスのオン/オフを決める。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <algorithm>

// 色収差の一時上書きの中身
struct ChromaticOverrideParams
{
    float strength = 0.0f; // 演出が出したい強さのピーク
};

struct ChromaticAberrationConfigComponent
{
    bool enable = true;

    // 中心（UV）/ 強さ / 中心からの減衰の鋭さ
    GpuData::CbChromaticData data{{0.5f, 0.5f}, 0.0f, 2.0f};

    PostEffectOverrides<ChromaticOverrideParams> overrides;

    /**
     * @brief 描画に渡す強さを返す。普段の値と、効いている全スロットの最大値をとる。
     *
     * 足し合わせないのは、演出が重なったときに振り切れて画面が崩れるのを防ぐため（強い演出が前に出る）。
     *
     * @return 色収差の強さ。
     */
    float ResolvedStrength() const
    {
        float out = enable ? data.chromaStrength : 0.0f;
        overrides.ForEachActive([&out](const ChromaticOverrideParams &p, float t) {
            out = (std::max)(out, p.strength * t);
        });
        return out;
    }
};
