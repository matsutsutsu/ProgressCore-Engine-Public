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
    // スロットの識別子と優先度。エンジンは演出の中身を知らないので、名前と優先度は使う側が決める。
    // 命中の演出（10）より高くして、同時に出たらジャスト回避の演出を優先する。
    const uint32_t kSource   = CCL::Utils::HashString("JustEvade");
    constexpr int  kPriority = 20;

    // スタイルごとの色の設定
    struct StylePreset
    {
        XMFLOAT3 tint;       // ColorGrading の乗算色
        float    saturation; // 彩度（1=そのまま, 0=白黒）
        float    contrast;   // コントラスト
        XMFLOAT3 vigColor;   // ビネットの色
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

    // hold（切り払いの準備中）の間は、timer に関係なくモノクロを保つ
    const bool holding = (fx && fx->hold);
    const bool active  = (fx && (fx->hold || fx->timer > 0.0f));

    auto *cg  = m_world->GetSingletonComponent<ColorGradingConfigComponent>();
    auto *rb  = m_world->GetSingletonComponent<RadialBlurConfigComponent>();
    auto *ca  = m_world->GetSingletonComponent<ChromaticAberrationConfigComponent>();
    auto *vig = m_world->GetSingletonComponent<VignetteConfigComponent>();

    // 演出が終わったフレームで、自分のスロットだけを手放す（設定本体と他の演出には触れない）
    if (!active)
    {
        if (m_wasActive)
        {
            if (cg) cg->overrides.Release(kSource);
            if (vig) vig->overrides.Release(kSource);
            if (rb) rb->overrides.Release(kSource);
            if (ca) ca->overrides.Release(kSource);
            m_wasActive = false;
        }
        return;
    }

    // 1. timer を減らす。dt は実時間で、スローの残り時間も実時間で減るので、演出とスローの長さが一致する。
    //    hold 中は減らさない
    if (!holding)
    {
        fx->timer -= dt;
        if (fx->timer < 0.0f) fx->timer = 0.0f;
    }

    // 2. 強さの時間変化を求める。
    //    body  : 色調とビネット用。0 → 1 に上がり、しばらく 1 を保ち、最後に 0 へ戻る
    //    spike : 放射ブラーと色収差用。始まった瞬間が最大で、spikeDecay 秒で 0 になる
    //    hold 中は body = 1・spike = 0 にし、style に関係なくモノクロにする
    float       body;
    float       spike;
    StylePreset preset;
    if (holding)
    {
        body   = 1.0f;
        spike  = 0.0f;
        preset = GetStylePreset(static_cast<uint8_t>(JustEvadeStyle::Monochrome));
    }
    else
    {
        const float elapsed = fx->duration - fx->timer; // 始まってからの時間（秒）
        if (fx->attackTime > 0.0f && elapsed < fx->attackTime)
            body = elapsed / fx->attackTime;
        else if (fx->releaseTime > 0.0f && fx->timer < fx->releaseTime)
            body = fx->timer / fx->releaseTime;
        else
            body = 1.0f;
        body = Sat01(body);

        spike  = (fx->spikeDecay > 0.0f) ? Sat01(1.0f - elapsed / fx->spikeDecay) : 0.0f;
        preset = GetStylePreset(fx->style);
    }

    // 3. 色調補正。スロットには「寄せたい値」と「寄せる割合（weight）」だけを書く。
    //    シーンの設定との補間は PostProcessSystem の Resolve() が行う。
    //    設定本体を書き換えて後で戻す形にしないのは、被弾フラッシュと同時に走ったとき、
    //    互いに書き換えた後の値を「元の値」として保存し合い、シーンの設定が壊れるため
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

    // 4. 放射ブラー
    if (rb)
    {
        if (auto *ov = rb->overrides.Acquire(kSource, kPriority))
        {
            ov->weight          = spike;
            ov->params.strength = fx->radialPeak;
        }
    }

    // 5. 色収差
    if (ca)
    {
        if (auto *ov = ca->overrides.Acquire(kSource, kPriority))
        {
            ov->weight          = spike;
            ov->params.strength = fx->chromaPeak;
        }
    }

    // 6. ビネット。半径とぼかしはシーンの設定のまま使いたいので、上書きの値にも同じ値を入れる。
    //    明滅の位相（vigTime）は PostProcessSystem が持つので触らない
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

// カメラが決まった後、PostProcessSystem（R09）の前に走らせる
REGISTER_RENDER_SYSTEM(JustEvadeEffectSystem, Priority::RenderStage::R02_Environment);
