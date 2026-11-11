#pragma once
// 色調補正の設定（シングルトン）。シーンの普段の設定と、演出ごとの一時的な上書き（overrides）を持つ
// 演出は enable / data を書き換えず、自分のスロットだけを触る。仕組みは PostEffectOverrides.h を参照
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <DirectXMath.h>
#include <cmath>

// 色調補正の一時上書きの中身。寄せる先の目標値だけを持つ（どれだけ寄せるかは weight）
struct ColorGradingOverrideParams
{
    DirectX::XMFLOAT4 tint       = {1.0f, 1.0f, 1.0f, 1.0f}; // 色味
    float             saturation = 1.0f;                     // 彩度
    float             contrast   = 1.0f;                     // コントラスト

    // この演出が求める mix のピーク。普段の mix より小さくはしない
    // mix は「色調補正をどれだけ画面に効かせるか」の全体の強さなので、
    // 演出で普段の設定より弱めてはいけない。そのため他の項目と違い最大値で合わせる
    float mixPeak = 0.0f;
};

struct ColorGradingConfigComponent
{
    bool enable = true;

    // 色味 / 彩度 / コントラスト / 明るさ / mix
    GpuData::CbColorGradingData data{
        {1.0f, 1.0f, 1.0f, 1.0f},
        1.0f,
        1.0f,
        1.0f,
        1.0f};

    PostEffectOverrides<ColorGradingOverrideParams> overrides;

    // enable=false でも、演出中は描く
    bool ResolvedEnable() const { return enable || overrides.AnyActive(); }

    // 上書きの土台になる、今の設定そのままの値（新しく取ったスロットへ PostFx::Acquire が入れる）
    ColorGradingOverrideParams BaseParams() const
    {
        ColorGradingOverrideParams p;
        p.tint       = data.gradeTint;
        p.saturation = data.gradeSaturation;
        p.contrast   = data.gradeContrast;
        p.mixPeak    = data.gradeMixAmount;
        return p;
    }

    // 描画に渡すパラメータ。普段の設定に、上書きを priority の小さい順に重ねる（明るさはどの演出も触らない）
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
