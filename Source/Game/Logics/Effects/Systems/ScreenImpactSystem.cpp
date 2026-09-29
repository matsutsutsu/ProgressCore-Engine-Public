#include "ScreenImpactSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Core/Math/StringHash.h"

using namespace CCL::ECS;

namespace
{
    // スロットの識別子と優先度。エンジンは演出の中身を知らないので、名前と優先度は使う側が決める。
    // ジャスト回避の演出（20）より低くして、同時に出たらジャスト回避を優先する。
    const uint32_t kSource   = CCL::Utils::HashString("ScreenImpact");
    constexpr int  kPriority = 10;
} // namespace

void ScreenImpactSystem::Update(float dt)
{
    auto *impact = m_world->GetSingletonComponent<ScreenImpactComponent>();

    const bool active = (impact && impact->timer > 0.0f);

    // 自分のスロットだけを書く（設定本体には触れない）
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

    // 演出が無い間は weight を 0 にするのではなくスロットごと手放し、他の演出に枠を空ける
    auto releaseBurst = [&]() {
        if (auto *c = m_world->GetSingletonComponent<RadialBlurConfigComponent>())
            c->overrides.Release(kSource);
        if (auto *c = m_world->GetSingletonComponent<ChromaticAberrationConfigComponent>())
            c->overrides.Release(kSource);
    };

    if (!active)
    {
        if (m_wasActive)
        {
            releaseBurst();
            m_wasActive = false;
        }
        return;
    }

    // dt は実時間。ヒットストップでゲームの時間が止まっている間も演出は進む
    impact->timer -= dt;
    if (impact->timer < 0.0f) impact->timer = 0.0f;

    const float t        = (impact->duration > 0.0f) ? (impact->timer / impact->duration) : 0.0f;
    const float strength = t * impact->power; // 命中した瞬間が最大で、残り時間に比例して弱くなる

    writeBurst(impact->radialStrength * strength, impact->chromaticStrength * strength);

    m_wasActive = true;
}

// カメラが決まった後、PostProcessSystem（R09）の前に走らせる
REGISTER_RENDER_SYSTEM(ScreenImpactSystem, Priority::RenderStage::R02_Environment);
