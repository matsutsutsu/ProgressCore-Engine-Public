#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <algorithm>

/// @brief 色収差の「素の設定」と、一時演出からの上乗せを保持する singleton。
///
/// @details
///  enable / data … シーンが持つ定常設定。演出システムは書き換えない。
///  burst*        … 演出ごとの一時的な上乗せ。各システムは自分のスロットだけを書く。
///  最終強度は PostProcessSystem が ResolvedStrength() で合成する。
///
///  スロットを分けている理由:
///    以前は3つの演出システムが data.chromaStrength を直接書いていたため、
///    同時に走ると後勝ちで消し合い、さらに演出終了時に enable=false / strength=0 を
///    書き込んで進行中の別演出まで打ち消していた（被弾フラッシュが出ない不具合）。
struct ChromaticAberrationConfigComponent
{
    bool enable = true;

    // choromaCenter, chromaStrength, chromaFalloff
    GpuData::CbChromaticData data{ { 0.5f, 0.5f }, 0.0f, 2.0f };

    float burstImpact    = 0.0f; ///< 命中バースト（ScreenImpactSystem）
    float burstJustEvade = 0.0f; ///< ジャスト回避スパイク（JustEvadeEffectSystem）
    float burstDamage    = 0.0f; ///< 被弾フラッシュ（PlayerFeedbackSystem）

    /// @brief 実際に描画へ渡す強度。素の値と全演出スロットの最大値。
    /// @note  加算ではなく最大値なのは、演出が重なったときに過剰に振り切れて
    ///        画面が破綻するのを避けるため（強い演出が前に出る）。
    float ResolvedStrength() const
    {
        const float base = enable ? data.chromaStrength : 0.0f;
        return (std::max)({base, burstImpact, burstJustEvade, burstDamage});
    }
};
