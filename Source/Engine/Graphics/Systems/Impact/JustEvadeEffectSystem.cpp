// ===========================================================================
// JustEvadeEffectSystem.cpp
// 役割・設計方針は JustEvadeEffectSystem.h を参照。
//
// 実装の意図(Why):
//   - body エンベロープ（アタック→サステイン→リリース）で色調とビネットを出し入れし、
//     spike エンベロープ（t=0最大→即減衰）で放射ブラー/色収差の「バキッ」を作る。
//   - dt はフレーム実時間。スロー窓(globalHitStopTimer)も実時間で減るので、演出の
//     長さ(duration)と体感のスロー時間が一致する。
//   - 立ち上がりで元の ColorGrading/Vignette を退避、立ち下がりで完全復元することで、
//     シーン側の色調設定を壊さない。
// ===========================================================================
#include "JustEvadeEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Components/Impact/JustEvadeEffectComponent.h"
#include "Engine/Graphics/Components/PostProcess/ColorGradingConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"

#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX;

namespace
{
// スタイルごとの色プリセット（脱色先・色味・コントラスト・ビネット色）。
struct StylePreset
{
    XMFLOAT3 tint;      // ColorGrading の乗算色
    float    saturation; // 彩度（1=そのまま, 0=白黒）
    float    contrast;   // コントラスト
    XMFLOAT3 vigColor;    // ビネットの色
};

StylePreset GetStylePreset(uint8_t style)
{
    switch (static_cast<JustEvadeStyle>(style))
    {
    case JustEvadeStyle::WitchPurple:
        return {{0.80f, 0.50f, 1.00f}, 0.30f, 1.05f, {0.08f, 0.02f, 0.12f}};
    case JustEvadeStyle::Monochrome:
        return {{0.90f, 0.95f, 1.00f}, 0.05f, 1.20f, {0.00f, 0.00f, 0.00f}};
    case JustEvadeStyle::CoolBlue:
    default:
        return {{0.55f, 0.75f, 1.00f}, 0.25f, 1.05f, {0.02f, 0.05f, 0.12f}};
    }
}

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Sat01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline XMFLOAT4 LerpTint(const XMFLOAT4 &a, const XMFLOAT3 &b, float t)
{
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), 1.0f};
}
} // namespace

void JustEvadeEffectSystem::Update(float dt)
{
    auto *fx = _world->GetSingletonComponent<JustEvadeEffectComponent>();

    const bool active = (fx && fx->timer > 0.0f);

    auto *cg  = _world->GetSingletonComponent<ColorGradingConfigComponent>();
    auto *rb  = _world->GetSingletonComponent<RadialBlurConfigComponent>();
    auto *ca  = _world->GetSingletonComponent<ChromaticAberrationConfigComponent>();
    auto *vig = _world->GetSingletonComponent<VignetteConfigComponent>();

    // ── 立ち下がり: 退避しておいた元の値へ完全復元し、スパイク系は OFF にして手放す ──
    if (!active)
    {
        if (_wasActive)
        {
            if (cg)
            {
                cg->enable              = _base.cgEnable;
                cg->data.gradeSaturation = _base.cgSat;
                cg->data.gradeContrast   = _base.cgContrast;
                cg->data.gradeBrightness = _base.cgBright;
                cg->data.gradeTint       = _base.cgTint;
                cg->data.gradeMixAmount  = _base.cgMix;
            }
            if (vig)
            {
                vig->enable            = _base.vigEnable;
                vig->data.vigIntensity  = _base.vigIntensity;
                vig->data.vigColor      = _base.vigColor;
                vig->data.vigPulseSpeed = _base.vigPulse;
            }
            if (rb) { rb->enable = false; rb->data.blurStrength   = 0.0f; }
            if (ca) { ca->enable = false; ca->data.chromaStrength = 0.0f; }
            _wasActive = false;
        }
        return;
    }

    // ── 立ち上がり: 元の ColorGrading / Vignette を退避（演出後にここへ戻す） ──
    if (!_wasActive)
    {
        if (cg)
        {
            _base.cgEnable   = cg->enable;
            _base.cgSat      = cg->data.gradeSaturation;
            _base.cgContrast = cg->data.gradeContrast;
            _base.cgBright   = cg->data.gradeBrightness;
            _base.cgTint     = cg->data.gradeTint;
            _base.cgMix      = cg->data.gradeMixAmount;
        }
        if (vig)
        {
            _base.vigEnable    = vig->enable;
            _base.vigIntensity = vig->data.vigIntensity;
            _base.vigColor     = vig->data.vigColor;
            _base.vigPulse     = vig->data.vigPulseSpeed;
        }
        _vigTime = 0.0f;
    }

    // ── 1. 減衰（実時間） ──
    fx->timer -= dt;
    if (fx->timer < 0.0f) fx->timer = 0.0f;
    _vigTime += dt;

    // ── 2. エンベロープ算出 ──
    const float elapsed = fx->duration - fx->timer; // 0 で発火、増えていく
    //  body: アタック(0→1) → サステイン(1) → リリース(1→0)
    float body;
    if (fx->attackTime > 0.0f && elapsed < fx->attackTime)
        body = elapsed / fx->attackTime;
    else if (fx->releaseTime > 0.0f && fx->timer < fx->releaseTime)
        body = fx->timer / fx->releaseTime;
    else
        body = 1.0f;
    body = Sat01(body);

    //  spike: t=0 最大 → spikeDecay 秒で 0（放射ブラー/色収差の一撃）
    const float spike = (fx->spikeDecay > 0.0f) ? Sat01(1.0f - elapsed / fx->spikeDecay) : 0.0f;

    const StylePreset preset = GetStylePreset(fx->style);

    // ── 3. 色調（ColorGrading）: base → style を body で補間。mix を上げて反映 ──
    if (cg)
    {
        cg->enable               = true;
        cg->data.gradeSaturation = Lerp(_base.cgSat, preset.saturation, body);
        cg->data.gradeContrast   = Lerp(_base.cgContrast, preset.contrast, body);
        cg->data.gradeBrightness = _base.cgBright; // 明るさは元のまま
        cg->data.gradeTint       = LerpTint(_base.cgTint, preset.tint, body);
        // 元の mix を下回らないようにしつつ、演出のピークまで持ち上げる
        cg->data.gradeMixAmount  = (std::max)(_base.cgMix, body * fx->gradeMixPeak);
    }

    // ── 4. 放射ブラー（スパイク） ──
    if (rb)
    {
        rb->enable           = true;
        rb->data.blurCenter  = XMFLOAT2(0.5f, 0.5f);
        rb->data.blurStrength = fx->radialPeak * spike;
        rb->data.blurFalloff  = 2.0f;
    }

    // ── 5. 色収差（スパイク） ──
    if (ca)
    {
        ca->enable             = true;
        ca->data.chromaCenter  = XMFLOAT2(0.5f, 0.5f);
        ca->data.chromaStrength = fx->chromaPeak * spike;
        ca->data.chromaFalloff  = 2.0f;
    }

    // ── 6. ビネット脈動（body で強度・色を出し入れ） ──
    if (vig)
    {
        vig->enable            = true;
        vig->data.vigIntensity  = Lerp(_base.vigIntensity, fx->vignettePeak, body);
        vig->data.vigColor      = LerpTint(_base.vigColor, preset.vigColor, body);
        vig->data.vigPulseSpeed = Lerp(_base.vigPulse, fx->vignettePulse, body);
        vig->data.vigTime       = _vigTime;
    }

    _wasActive = true;
}

// PostProcessSystem(R09) の前・カメラ確定後に走らせる（ScreenImpact と同じ R02）。
REGISTER_RENDER_SYSTEM(JustEvadeEffectSystem, Priority::RenderStage::R02_Environment);
