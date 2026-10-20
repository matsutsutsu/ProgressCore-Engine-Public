#include "Game/Logics/Tutorial/TutorialPlayerGuardSystem.h"

#include <algorithm> // std::max

void TutorialPlayerGuardSystem::Update(float /*dt*/)
{
    // PlayerComponent を持つのはプレイヤーだけなので、ボスとドローンの HP には触らない
    ForEach([](HealthComponent &health, const PlayerComponent &) {
        health.currentHealth = std::max(health.currentHealth, kMinHealth);
    });
}

// 全体登録（REGISTER_*）はせず、TutorialScene が登録する。ゲーム本編では倒れるようにするため
