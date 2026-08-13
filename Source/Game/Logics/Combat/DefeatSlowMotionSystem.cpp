/// @file DefeatSlowMotionSystem.cpp
/// 役割・設計方針は DefeatSlowMotionSystem.h を参照。
#include "DefeatSlowMotionSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h" // 全体スローは意図ファサード経由

#include "Game/Logics/Character/Player/Components/PlayerComponent.h"   // プレイヤー識別
#include "Game/Logics/Character/Boss/Data/BossActionComponent.h"      // ボス識別

using namespace CCL::ECS;

void DefeatSlowMotionSystem::Update(float dt)
{
    if (!m_world) return;

    if (m_cooldown > 0.0f) m_cooldown -= dt;

    bool normalKill    = false; // このフレームに敵を1体以上倒したか
    bool climax        = false; // ボス撃破 or ワールドクリア（大きいスロー）
    bool sawEnemyDeath = false; // ワールドクリア判定用（今フレームの敵撃破）
    int  aliveEnemies  = 0;     // 生存中の敵数（プレイヤー除く）

    ForEachWithID([&](EntityID id, HealthComponent &hp) {
        const bool isPlayer = m_world->HasComponent<PlayerComponent>(id);
        const bool dead     = (hp.currentHealth <= 0.0f);

        if (!isPlayer && !dead) ++aliveEnemies;

        // まだ演出していない死亡を「1回だけ」拾う。
        if (!hp.deathFxFired && dead)
        {
            hp.deathFxFired = true;
            if (!isPlayer)
            {
                sawEnemyDeath = true;
                normalKill    = true;
                // ボスを倒したらクライマックス扱い。
                if (m_world->HasComponent<BossActionComponent>(id)) climax = true;
            }
        }
    });

    // 最後の1体を倒した（＝残り敵0）のもクライマックス。
    if (sawEnemyDeath && aliveEnemies == 0) climax = true;

    if (climax)
    {
        Time::SlowWorld(*m_world, kClimaxScale, kClimaxDur);
        m_cooldown = kCooldown; // 直後に通常スローが重ならないように
    }
    else if (normalKill && m_cooldown <= 0.0f)
    {
        Time::SlowWorld(*m_world, kNormalScale, kNormalDur);
        m_cooldown = kCooldown;
    }
}

// L06(ダメージ適用) の後、死亡が確定したこのタイミングで検知する。
REGISTER_LOGIC_SYSTEM(DefeatSlowMotionSystem, Priority::LogicStage::L07_Cleanup);
