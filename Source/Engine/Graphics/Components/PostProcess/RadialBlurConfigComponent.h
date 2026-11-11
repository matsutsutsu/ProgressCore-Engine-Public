#pragma once
// 放射状ブラー（集中線）の設定（シングルトン）。シーンの普段の設定と、演出ごとの一時的な上乗せを持つ
// 演出は enable / data を書き換えず、自分のスロットだけを触る。data の保存は Meta の手書き
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <algorithm>

// 放射状ブラーの一時上書きの中身
struct RadialBlurOverrideParams
{
    float strength = 0.0f; // 演出が出したい強さのピーク
    float falloff  = 2.0f; // 中心からの立ち上がりの鋭さ。土台にシーンの値が入るので、書かなければシーンのまま
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

    // 描画に渡す値。強さは ResolvedStrength と同じく最大値をとり、減衰はその最大を出したスロットのものを使う
    // 減衰はスロットの効き（t）でシーンの値から寄せる。フェードの途中で別のスロットへ勝ちが移っても跳ばないようにするため
    GpuData::CbRadialBlurData Resolve() const
    {
        GpuData::CbRadialBlurData out = data;
        out.blurStrength              = enable ? data.blurStrength : 0.0f;

        overrides.ForEachActive([&](const RadialBlurOverrideParams &p, float t) {
            const float strength = p.strength * t;
            if (strength > out.blurStrength)
            {
                out.blurStrength = strength;
                out.blurFalloff  = data.blurFalloff + (p.falloff - data.blurFalloff) * t;
            }
        });
        return out;
    }

    // 上書きの土台になる、今効いている強さと減衰。enable=false なら強さは 0（理由は色収差と同じ）
    RadialBlurOverrideParams BaseParams() const
    {
        RadialBlurOverrideParams p;
        p.strength = enable ? data.blurStrength : 0.0f;
        p.falloff  = data.blurFalloff;
        return p;
    }
};
