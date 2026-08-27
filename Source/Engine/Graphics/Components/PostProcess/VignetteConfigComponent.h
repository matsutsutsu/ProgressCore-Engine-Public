#pragma once
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "Engine/Graphics/Components/PostProcess/PostEffectOverrides.h"

#include <DirectXMath.h>

/// @brief ビネットの一時上書きの中身。寄せ先の目標値だけを持つ（強さは weight 側）。
struct VignetteOverrideParams
{
    DirectX::XMFLOAT4 color      = {0.0f, 0.0f, 0.0f, 1.0f};
    float             intensity  = 0.0f;
    float             radius     = 0.55f;
    float             softness   = 0.35f;
    float             pulseSpeed = 0.0f;
};

/// @brief ビネットの「素の設定」と、演出からの一時的な上書きを保持する singleton。
/// @details enable / data はシーンが持つ定常設定で、演出システムは書き換えない。
///          最終値は PostProcessSystem が Resolve() で合成する。
///          スロットの仕組みと背景は PostEffectOverrides.h を参照。
struct VignetteConfigComponent
{
    bool enable = true;

    GpuData::CbVignetteData data{
        { 0.0f, 0.0f, 0.0f, 1.0f },
        0.4f,
        0.55f,
        0.35f,
        0.0f,
        0.0f
    };

    PostEffectOverrides<VignetteOverrideParams> overrides;

    /// @brief 素の設定が OFF でも、演出中は描画する必要がある。
    bool ResolvedEnable() const { return enable || overrides.AnyActive(); }

    /// @brief 描画へ渡す最終パラメータ。素の設定に上書きを priority 昇順で重ねる。
    /// @note  最大値合成ではなく重ね順にしているのは、ビネットが色・半径・脈動という
    ///        「組み合わせで意味を持つ」パラメータ群だから。各項目を独立に最大化すると
    ///        どの演出とも違う色と形になってしまう。
    /// @note  vigTime は演出ではなく PostProcessSystem が進める連続値なので触らない。
    GpuData::CbVignetteData Resolve() const
    {
        GpuData::CbVignetteData out = data;
        overrides.ForEachActive([&out](const VignetteOverrideParams &p, float t) {
            out.vigColor.x    = Lerp(out.vigColor.x, p.color.x, t);
            out.vigColor.y    = Lerp(out.vigColor.y, p.color.y, t);
            out.vigColor.z    = Lerp(out.vigColor.z, p.color.z, t);
            out.vigColor.w    = Lerp(out.vigColor.w, p.color.w, t);
            out.vigIntensity  = Lerp(out.vigIntensity, p.intensity, t);
            out.vigRadius     = Lerp(out.vigRadius, p.radius, t);
            out.vigSoftness   = Lerp(out.vigSoftness, p.softness, t);
            out.vigPulseSpeed = Lerp(out.vigPulseSpeed, p.pulseSpeed, t);
        });
        return out;
    }

private:
    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
};
