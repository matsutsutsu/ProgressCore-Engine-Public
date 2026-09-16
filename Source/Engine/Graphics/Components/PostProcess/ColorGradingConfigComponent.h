#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <DirectXMath.h>
#include <cmath> // std::lerp

// 色調補正の一時上書きの中身。寄せ先の目標値だけを持つ（強さは weight 側）。
struct ColorGradingOverrideParams
{
    DirectX::XMFLOAT4 tint       = {1.0f, 1.0f, 1.0f, 1.0f};
    float             saturation = 1.0f;
    float             contrast   = 1.0f;

    // この演出が要求する mix のピーク。素の mix を下回らせない。
    //
    // mix だけ他項目と扱いが違うのは「色調をどれだけ画面へ効かせるか」の
    // マスターつまみであり、素の設定より弱めてはいけないため。
    float mixPeak = 0.0f;
};

// 色調補正の「素の設定」と、演出からの一時的な上書きを保持する singleton。
//
// enable / data はシーンが持つ定常設定で、演出システムは書き換えない。
// スロットの仕組みと背景は PostEffectOverrides.h を参照。
struct ColorGradingConfigComponent
{
    bool enable = true;

    GpuData::CbColorGradingData data{
        {1.0f, 1.0f, 1.0f, 1.0f},
        1.0f,
        1.0f,
        1.0f,
        1.0f};

    PostEffectOverrides<ColorGradingOverrideParams> overrides;

    // 素の設定が OFF でも、演出中は描画する必要がある。
    bool ResolvedEnable() const { return enable || overrides.AnyActive(); }

    // 描画へ渡す最終パラメータ。素の設定に上書きを priority 昇順で重ねる。
    //
    // 明るさ(gradeBrightness)はどの演出も触らないので素の値のまま通す。
    GpuData::CbColorGradingData Resolve() const
    {
        GpuData::CbColorGradingData out = data;
        overrides.ForEachActive([&out](const ColorGradingOverrideParams &p, float t) {
            out.gradeTint.x     = std::lerp(out.gradeTint.x, p.tint.x, t);
            out.gradeTint.y     = std::lerp(out.gradeTint.y, p.tint.y, t);
            out.gradeTint.z     = std::lerp(out.gradeTint.z, p.tint.z, t);
            out.gradeTint.w     = std::lerp(out.gradeTint.w, p.tint.w, t);
            out.gradeSaturation = std::lerp(out.gradeSaturation, p.saturation, t);
            out.gradeContrast   = std::lerp(out.gradeContrast, p.contrast, t);

            const float mix = p.mixPeak * t;
            if (mix > out.gradeMixAmount)
                out.gradeMixAmount = mix;
        });
        return out;
    }
};
