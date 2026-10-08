#pragma once
// ===========================================================================
// 概要: 色調補正の設定と、演出からの一時的な上書きを持つシングルトンコンポーネント。
//
// 責務:
//   - シーンが持つ普段の設定（enable / data）を持つ
//   - 演出ごとの一時的な上書き（overrides）を持つ
//   - 両方を重ねた最終的なパラメータを求める（Resolve）
//
// 備考: 演出システムは enable / data を書き換えず、自分のスロットだけを触る。
//       スロットの仕組みと理由は PostEffectOverrides.h を参照。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <DirectXMath.h>
#include <cmath>

// 色調補正の一時上書きの中身。寄せる先の目標値だけを持つ（どれだけ寄せるかは weight）。
struct ColorGradingOverrideParams
{
    DirectX::XMFLOAT4 tint       = {1.0f, 1.0f, 1.0f, 1.0f}; // 色味
    float             saturation = 1.0f;                     // 彩度
    float             contrast   = 1.0f;                     // コントラスト

    // この演出が求める mix のピーク。普段の mix より小さくはしない。
    // mix は「色調補正をどれだけ画面に効かせるか」の全体の強さなので、
    // 演出で普段の設定より弱めてはいけない。そのため他の項目と違い最大値で合わせる。
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

    /**
     * @brief 描画に渡すパラメータを返す。普段の設定に、上書きを priority の小さい順に重ねる。
     * @return 定数バッファへ渡すパラメータ。明るさ（gradeBrightness）はどの演出も触らないので、普段の値のまま。
     */
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
