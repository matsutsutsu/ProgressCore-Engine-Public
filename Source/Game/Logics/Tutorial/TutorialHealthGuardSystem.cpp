#include "Game/Logics/Tutorial/TutorialHealthGuardSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
    constexpr float kPlayerMinHealth      = 1.0f;  // プレイヤーの HP の下限
    constexpr float kBossMinHealthPercent = 50.0f; // ボスの HP の下限（最大 HP に対する %）
} // namespace

std::vector<TypeID> TutorialHealthGuardSystem::GetReadTypes() const
{
    return {
        TypeInfo<PlayerComponent>::ID(),
        TypeInfo<BossPhaseComponent>::ID(),
    };
}



void TutorialHealthGuardSystem::Update(float /*dt*/)
{
    ForEachWithID([&](EntityID id, HealthComponent &health) {
        // --- プレイヤー: 倒れない ---
        if (m_world->HasComponent<PlayerComponent>(id))
        {
            health.currentHealth = std::max(health.currentHealth, kPlayerMinHealth);
            return;
        }

        // --- ボス: 倒れない ---

        // チュートリアルの形態（TutorialWait / TutorialAttack）は第2形態へ移らないので、
        // 第2形態の有無に関係なく同じ下限で止める
        if (!m_world->HasComponent<BossPhaseComponent>(id)) return;

        const float floor    = health.maxHealth * kBossMinHealthPercent / 100.0f;
        health.currentHealth = std::max(health.currentHealth, floor);
    });
}



// 全体登録（REGISTER_*）はせず、TutorialScene が登録する。ゲーム本編では倒れる・形態が移るようにするため
