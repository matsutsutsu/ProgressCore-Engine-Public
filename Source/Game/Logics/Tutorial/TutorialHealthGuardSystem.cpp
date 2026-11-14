#include "Game/Logics/Tutorial/TutorialHealthGuardSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
    constexpr float kPlayerMinHealth = 1.0f; // プレイヤーの HP の下限
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

        // --- ボス: 形態移行のしきい値を割らない ---

        // 第2形態の BT が未設定なら形態移行が起きないので、下限も設けない（BossPhaseSystem と同じ条件）
        const auto *phase = m_world->GetComponent<BossPhaseComponent>(id);
        if (!phase || phase->phase2AssetPath.empty()) return;

        // 形態が移るのはしきい値より下に落ちたときなので、ちょうどで止めれば移らない
        const float floor    = health.maxHealth * phase->phase2HealthThreshold / 100.0f;
        health.currentHealth = std::max(health.currentHealth, floor);
    });
}



// 全体登録（REGISTER_*）はせず、TutorialScene が登録する。ゲーム本編では倒れる・形態が移るようにするため
