#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <algorithm>

/// @brief 色収差の一時上書きの中身。演出が出したい強度のピーク。
struct ChromaticOverrideParams
{
    float strength = 0.0f;
};

/// @brief 色収差の「素の設定」と、演出からの一時的な上乗せを保持する singleton。
///
/// @details
///  enable / data … シーンが持つ定常設定。演出システムは書き換えない。
///  overrides     … 演出ごとの一時的な上乗せ。各システムは自分のスロットだけを触る。
///  最終強度は PostProcessSystem が ResolvedStrength() で合成する。
///  スロットの仕組みと背景は PostEffectOverrides.h を参照。
struct ChromaticAberrationConfigComponent
{
    bool enable = true;

    // choromaCenter, chromaStrength, chromaFalloff
    GpuData::CbChromaticData data{ { 0.5f, 0.5f }, 0.0f, 2.0f };

    PostEffectOverrides<ChromaticOverrideParams> overrides;

    /// @brief 実際に描画へ渡す強度。素の値と全演出スロットの最大値。
    /// @note  加算ではなく最大値なのは、演出が重なったときに過剰に振り切れて
    ///        画面が破綻するのを避けるため（強い演出が前に出る）。
    float ResolvedStrength() const
    {
        float out = enable ? data.chromaStrength : 0.0f;
        overrides.ForEachActive([&out](const ChromaticOverrideParams &p, float t) {
            out = (std::max)(out, p.strength * t);
        });
        return out;
    }
};
