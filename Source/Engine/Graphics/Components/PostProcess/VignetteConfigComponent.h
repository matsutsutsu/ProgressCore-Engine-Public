#pragma once
// ===========================================================================
// 概要: ビネットの設定と、演出からの一時的な上書きを持つシングルトンコンポーネント。
//
// 責務:
//   - シーンが持つ普段の設定（enable / data）を持つ
//   - 演出ごとの一時的な上書き（overrides）を持つ
//   - 両方を重ねた最終的なパラメータを求める（Resolve）
//
// 備考: 演出システムは enable / data を書き換えず、自分のスロットだけを触る。
//       最終的な値は PostProcessSystem が Resolve() で求める。
//       スロットの仕組みと理由は PostEffectOverrides.h を参照。
// ===========================================================================
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <DirectXMath.h>
#include <cmath>

// ビネットの一時上書きの中身。寄せる先の目標値だけを持つ（どれだけ寄せるかは weight）。
struct VignetteOverrideParams
{
    DirectX::XMFLOAT4 color      = {0.0f, 0.0f, 0.0f, 1.0f}; // 周辺を塗る色
    float             intensity  = 0.0f;                     // 強さ
    float             radius     = 0.55f;                    // かかり始める半径（画面中心から, UV）
    float             softness   = 0.35f;                    // 境目のぼかし幅
    float             pulseSpeed = 0.0f;                     // 脈打つ速さ（回/秒。0 で脈打たない）
};

struct VignetteConfigComponent
{
    bool enable = true;

    // 色 / 強さ / 半径 / ぼかし幅 / 脈打つ速さ / 時間
    GpuData::CbVignetteData data{
        {0.0f, 0.0f, 0.0f, 1.0f},
        0.4f,
        0.55f,
        0.35f,
        0.0f,
        0.0f};

    PostEffectOverrides<VignetteOverrideParams> overrides;

    // enable=false でも、演出中は描く
    bool ResolvedEnable() const { return enable || overrides.AnyActive(); }

    // 描画に渡すパラメータ。普段の設定に、上書きを priority の小さい順に重ねる。
    // 色収差のように最大値をとらないのは、ビネットの色・半径・脈動が組み合わせで
    // 意味を持つため。項目ごとに最大値をとると、どの演出とも違う色と形になる。
    // vigTime は演出ではなく PostProcessSystem が進める値なので触らない。
    GpuData::CbVignetteData Resolve() const
    {
        GpuData::CbVignetteData out = data;
        overrides.ForEachActive([&out](const VignetteOverrideParams &p, float t) {
            out.vigColor.x    = std::lerp(out.vigColor.x, p.color.x, t);
            out.vigColor.y    = std::lerp(out.vigColor.y, p.color.y, t);
            out.vigColor.z    = std::lerp(out.vigColor.z, p.color.z, t);
            out.vigColor.w    = std::lerp(out.vigColor.w, p.color.w, t);
            out.vigIntensity  = std::lerp(out.vigIntensity, p.intensity, t);
            out.vigRadius     = std::lerp(out.vigRadius, p.radius, t);
            out.vigSoftness   = std::lerp(out.vigSoftness, p.softness, t);
            out.vigPulseSpeed = std::lerp(out.vigPulseSpeed, p.pulseSpeed, t);
        });
        return out;
    }
};
