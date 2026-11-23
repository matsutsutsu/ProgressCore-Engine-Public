#pragma once
// 色収差の設定（シングルトン）。シーンの普段の設定と、演出ごとの一時的な上乗せ（overrides）を持つ
// 演出は enable / data を書き換えず、自分のスロットだけを触る。仕組みは PostEffectOverrides.h を参照
// enable が false でも演出中は描くので、PostProcessSystem は合成後の強さでパスのオン/オフを決める
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

    // 描画に渡す強さ。普段の値と、効いている全スロットのうちの最大をとる
    // 足し合わせると、演出が重なったときに振り切れて画面が崩れる
    float ResolvedStrength() const
    {
        float out = enable ? data.chromaStrength : 0.0f;
        for (const auto *s : overrides.SortedActive())
            out = (std::max)(out, s->params.strength * s->Amount());
        return out;
    }

    // 新しい枠の土台にする、今効いている強さ（Acquire の base に渡す）。enable が false なら 0
    // 切ってある強さを土台に入れると、演出が weight を上げただけで、切ったはずの色収差が出てしまう
    ChromaticOverrideParams BaseParams() const
    {
        ChromaticOverrideParams p;
        p.strength = enable ? data.chromaStrength : 0.0f;
        return p;
    }
};
