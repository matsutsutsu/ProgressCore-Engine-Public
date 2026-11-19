#pragma once
// 被写界深度の設定（シングルトン）。シーンの普段の設定と、演出ごとの一時的な上書き（overrides）を持つ。既定はオフ
// 演出（ムービーのカメラなど）は enable / data を書き換えず、自分のスロットだけを触る。data の保存は Meta の手書き
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

// 被写界深度の一時上書きの中身
struct DepthOfFieldOverrideParams
{
    float focusDistance = 10.0f; // ピントの合う距離（m）
    float focusRange    = 3.0f;  // ピントの前後でぼけない幅（m）
    float falloff       = 8.0f;  // ぼけない幅の外から、最大のぼけに達するまでの距離（m）
    float maxBlur       = 0.0f;  // 最大のぼけの半径（UV）。0 ならぼかさない
};

struct DepthOfFieldConfigComponent
{
    bool enable = false;

    // ピント位置 10m / その前後 3m はぼけない / そこから最大のぼけまで 8m / 最大のぼけ半径 0.01（UV）
    GpuData::CbDepthOfFieldData data{10.0f, 3.0f, 8.0f, 0.01f};

    PostEffectOverrides<DepthOfFieldOverrideParams> overrides;

    // 描画に渡す値。普段の値から、効いているスロットへ priority の小さい順に寄せる
    // ぼけの強さも寄せるので、enable=false の土台（ぼけ 0）からでも、効き（t）に合わせてじわっとぼける
    GpuData::CbDepthOfFieldData Resolve() const
    {
        DepthOfFieldOverrideParams out = BaseParams();

        for (const auto *s : overrides.SortedActive())
        {
            const auto &p = s->params;
            const float t = s->Amount();
            out.focusDistance += (p.focusDistance - out.focusDistance) * t;
            out.focusRange += (p.focusRange - out.focusRange) * t;
            out.falloff += (p.falloff - out.falloff) * t;
            out.maxBlur += (p.maxBlur - out.maxBlur) * t;
        }

        return {out.focusDistance, out.focusRange, out.falloff, out.maxBlur};
    }

    // 上書きの土台になる、今効いている値（Acquire の base に渡す）。enable=false ならぼけは 0（シーンではぼかしていないため）
    DepthOfFieldOverrideParams BaseParams() const
    {
        DepthOfFieldOverrideParams p;
        p.focusDistance = data.dofFocusDistance;
        p.focusRange    = data.dofFocusRange;
        p.falloff       = data.dofFalloff;
        p.maxBlur       = enable ? data.dofMaxBlur : 0.0f;
        return p;
    }
};
