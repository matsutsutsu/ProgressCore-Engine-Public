// ===========================================================================
// JustEvadeEffectSystem.cpp
// 役割・設計方針は JustEvadeEffectSystem.h を参照。
//
// 実装の意図(Why):
//   - body エンベロープ（アタック→サステイン→リリース）で色調とビネットを出し入れし、
//     spike エンベロープ（t=0最大→即減衰）で放射ブラー/色収差の「バキッ」を作る。
//   - dt はフレーム実時間。スロー窓(globalHitStopTimer)も実時間で減るので、演出の
//     長さ(duration)と体感のスロー時間が一致する。
//   - PP 設定は直接書き換えず、自分のキー("JustEvade")で確保したスロットへ書く。
//     素の設定との合成は PostProcessSystem が行う。退避と復元をやめたのは、
//     被弾フラッシュと同時に走ったとき互いの変更後の値を「元の値」として保存し合い、
//     シーンの設定を壊してしまうため。
// ===========================================================================
#include "JustEvadeEffectSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h"
#include "Engine/Graphics/Components/PostProcess/ColorGradingConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/VignetteConfigComponent.h"

#include "Engine/Core/Math/StringHash.h"

#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX;

namespace
{
// スロットの識別子と重ね順。エンジンは中身を知らないので、名前と優先度は使う側が決める。
// 命中バースト(10)より強くしておき、同時に出たら回避側が前に出るようにする。
const uint32_t kSource   = CCL::Utils::HashString("JustEvade");
constexpr int  kPriority = 20;

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

inline float Sat01(float v) { return std::clamp(v, 0.0f, 1.0f); }
} // namespace

std::vector<CCL::ECS::TypeID> JustEvadeEffectSystem::GetReadTypes() const
{
    return {TypeInfo<JustEvadeEffectComponent>::ID()};
}

std::vector<CCL::ECS::TypeID> JustEvadeEffectSystem::GetWriteTypes() const
{
    return {TypeInfo<ColorGradingConfigComponent>::ID(),
            TypeInfo<RadialBlurConfigComponent>::ID(),
            TypeInfo<ChromaticAberrationConfigComponent>::ID(),
            TypeInfo<VignetteConfigComponent>::ID()};
}

void JustEvadeEffectSystem::Update(float dt)
{
    auto *fx = m_world->GetSingletonComponent<JustEvadeEffectComponent>();

    // hold=true（切り払い準備の照準中）は timer に依らずモノクロを保持し続ける。
    const bool holding = (fx && fx->hold);
    const bool active  = (fx && (fx->hold || fx->timer > 0.0f));

    auto *cg  = m_world->GetSingletonComponent<ColorGradingConfigComponent>();
    auto *rb  = m_world->GetSingletonComponent<RadialBlurConfigComponent>();
    auto *ca  = m_world->GetSingletonComponent<ChromaticAberrationConfigComponent>();
    auto *vig = m_world->GetSingletonComponent<VignetteConfigComponent>();

    // ── 立ち下がり: 自分のスロットだけ手放す（設定本体・他演出には一切触れない） ──
    if (!active)
    {
        if (m_wasActive)
        {
            if (cg)  cg->overrides.Release(kSource);
            if (vig) vig->overrides.Release(kSource);
            if (rb)  rb->overrides.Release(kSource);
            if (ca)  ca->overrides.Release(kSource);
            m_wasActive = false;
        }
        return;
    }

    // ── 1. 減衰（実時間）。hold 中は timer を進めず保持する ──
    if (!holding)
    {
        fx->timer -= dt;
        if (fx->timer < 0.0f) fx->timer = 0.0f;
    }

    // ── 2. エンベロープ算出 ──
    //  hold 中：モノクロを body=1 で維持し、スパイク(放射ブラー/色収差)は出さない。
    //           色スタイルも serialized な fx->style に依らず Monochrome を強制する。
    float body;
    float spike;
    StylePreset preset;
    if (holding)
    {
        body   = 1.0f;
        spike  = 0.0f;
        preset = GetStylePreset(static_cast<uint8_t>(JustEvadeStyle::Monochrome));
    }
    else
    {
        const float elapsed = fx->duration - fx->timer; // 0 で発火、増えていく
        //  body: アタック(0→1) → サステイン(1) → リリース(1→0)
        if (fx->attackTime > 0.0f && elapsed < fx->attackTime)
            body = elapsed / fx->attackTime;
        else if (fx->releaseTime > 0.0f && fx->timer < fx->releaseTime)
            body = fx->timer / fx->releaseTime;
        else
            body = 1.0f;
        body = Sat01(body);

        //  spike: t=0 最大 → spikeDecay 秒で 0（放射ブラー/色収差の一撃）
        spike  = (fx->spikeDecay > 0.0f) ? Sat01(1.0f - elapsed / fx->spikeDecay) : 0.0f;
        preset = GetStylePreset(fx->style);
    }

    // ── 3. 色調（ColorGrading）: 目標値を weight=body で乗せる ──
    //   素の設定からの補間は PostProcessSystem 側の Resolve() が行うので、
    //   ここでは「どこへ寄せたいか」と「どれだけ寄せるか」だけを書く。
    if (cg)
    {
        if (auto *ov = cg->overrides.Acquire(kSource, kPriority))
        {
            ov->weight            = body;
            ov->params.saturation = preset.saturation;
            ov->params.contrast   = preset.contrast;
            ov->params.tint       = {preset.tint.x, preset.tint.y, preset.tint.z, 1.0f};
            ov->params.mixPeak    = fx->gradeMixPeak;
        }
    }

    // ── 4. 放射ブラー（スパイク） ──
    if (rb)
    {
        if (auto *ov = rb->overrides.Acquire(kSource, kPriority))
        {
            ov->weight          = spike;
            ov->params.strength = fx->radialPeak;
        }
    }

    // ── 5. 色収差（スパイク） ──
    if (ca)
    {
        if (auto *ov = ca->overrides.Acquire(kSource, kPriority))
        {
            ov->weight          = spike;
            ov->params.strength = fx->chromaPeak;
        }
    }

    // ── 6. ビネット脈動（body で強度・色を出し入れ） ──
    //   半径/柔らかさは素の設定のまま使いたいので、上書き値にも素の値をそのまま入れる。
    //   脈動の位相(vigTime)は PostProcessSystem が所有するのでここでは触らない。
    if (vig)
    {
        if (auto *ov = vig->overrides.Acquire(kSource, kPriority))
        {
            ov->weight            = body;
            ov->params.intensity  = fx->vignettePeak;
            ov->params.color      = {preset.vigColor.x, preset.vigColor.y, preset.vigColor.z, 1.0f};
            ov->params.pulseSpeed = fx->vignettePulse;
            ov->params.radius     = vig->data.vigRadius;
            ov->params.softness   = vig->data.vigSoftness;
        }
    }

    m_wasActive = true;
}

// PostProcessSystem(R09) の前・カメラ確定後に走らせる（ScreenImpact と同じ R02）。
REGISTER_RENDER_SYSTEM(JustEvadeEffectSystem, Priority::RenderStage::R02_Environment);
