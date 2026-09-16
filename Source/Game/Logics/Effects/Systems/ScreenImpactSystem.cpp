// ===========================================================================
// ScreenImpactSystem.cpp
// 役割・設計方針は ScreenImpactSystem.h を参照。
//
// 実装の意図(Why):
//   - 強さ strength = (timer/duration) * power。残り時間で線形に減衰し、命中直後が最大。
//   - dt はフレーム実時間。ヒットストップ中(ゲーム時間停止)でもバーストは進むため、
//     「時が止まって画面だけ荒れる」定番の手触りになる。
//   - 画面の揺れはここでは扱わない。MainCamera を直接書くと CameraBrain が確定させた
//     結果の上に加算する形になり、CameraShake 経由の他の揺れ（着地・被弾）と合成規則が
//     食い違う。命中時の揺れは DamageResolutionSystem が CameraShakeRequestEvent を
//     出す1本に揃えてある。
// ===========================================================================
#include "ScreenImpactSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Core/Math/StringHash.h"

using namespace CCL::ECS;

namespace
{
    // スロットの識別子と重ね順。エンジンは中身を知らないので、名前と優先度は使う側が決める。
    // ジャスト回避(20)より弱くしておき、同時に出たら回避側が前に出るようにする。
    const uint32_t kSource   = CCL::Utils::HashString("ScreenImpact");
    constexpr int  kPriority = 10;
} // namespace

void ScreenImpactSystem::Update(float dt)
{
    auto *impact = m_world->GetSingletonComponent<ScreenImpactComponent>();

    const bool active = (impact && impact->timer > 0.0f);

    // ── PP への要求（自分のスロットだけを書く。設定本体には触れない） ──
    //   非アクティブ時は weight=0 ではなくスロットごと手放し、他の演出に枠を空ける。
    auto writeBurst = [&](float radialStrength, float chromaStrength) {
        if (auto *c = m_world->GetSingletonComponent<RadialBlurConfigComponent>())
        {
            if (auto *ov = c->overrides.Acquire(kSource, kPriority))
            {
                ov->weight          = 1.0f;
                ov->params.strength = radialStrength;
            }
        }
        if (auto *c = m_world->GetSingletonComponent<ChromaticAberrationConfigComponent>())
        {
            if (auto *ov = c->overrides.Acquire(kSource, kPriority))
            {
                ov->weight          = 1.0f;
                ov->params.strength = chromaStrength;
            }
        }
    };

    auto releaseBurst = [&]() {
        if (auto *c = m_world->GetSingletonComponent<RadialBlurConfigComponent>())
            c->overrides.Release(kSource);
        if (auto *c = m_world->GetSingletonComponent<ChromaticAberrationConfigComponent>())
            c->overrides.Release(kSource);
    };

    if (!active)
    {
        // 立ち下がり: 自分のスロットを手放す（設定本体・他演出には触れない）。
        if (m_wasActive)
        {
            releaseBurst();
            m_wasActive = false;
        }
        return;
    }

    // ── 1. 減衰 ──
    impact->timer -= dt;
    if (impact->timer < 0.0f) impact->timer = 0.0f;

    const float t        = (impact->duration > 0.0f) ? (impact->timer / impact->duration) : 0.0f;
    const float strength = t * impact->power; // 命中直後=最大 → 0 へ

    // ── 2. PPバースト（放射ブラー＋色収差） ──
    writeBurst(impact->radialStrength * strength, impact->chromaticStrength * strength);

    m_wasActive = true;
}

// CameraBrainSystem(R01b_CameraBrain) の後、PostProcessSystem(R09) の前に走る。
REGISTER_RENDER_SYSTEM(ScreenImpactSystem, Priority::RenderStage::R02_Environment);
