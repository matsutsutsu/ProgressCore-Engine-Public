#include "Game/Logics/Combat/Feedback/DefeatSlowMotionSystem.h"
#include "Game/Logics/Combat/Components/EnemyTag.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/TimeService.h"
#include "Engine/Cinematic/CinematicEvents.h" // 撃破ムービーの再生要求

#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"

using namespace CCL::ECS;

namespace
{
    constexpr const char *kDefeatCinematic = "Assets/Cinematics/BossDefeat.json"; // 撃破ムービーの台本
}

void DefeatSlowMotionSystem::Update(float dt)
{
    if (!m_world) return;

    if (m_cooldown > 0.0f) m_cooldown -= dt;

    bool normalKill    = false; // このフレームで敵を1体以上倒したか
    bool climax        = false; // ボスか最後の敵を倒したか（大きいスロー）
    bool bossDefeated  = false; // このフレームでボスを倒したか（撃破ムービー）
    bool sawEnemyDeath = false; // このフレームで敵が倒れたか（最後の1体かを見るため）
    int  aliveEnemies  = 0;     // 生きている敵の数

    ForEachWithID([&](EntityID id, HealthComponent &hp) {
        // 敵かどうかは EnemyTag だけで決める（理由は EnemyTag.h を参照）
        const bool isEnemy = m_world->HasComponent<Tag::EnemyTag>(id);
        const bool dead    = (hp.currentHealth <= 0.0f);

        if (isEnemy && !dead) ++aliveEnemies;

        // まだスローを出していない倒れた敵を、1回だけ拾う
        if (!hp.deathFxFired && dead)
        {
            hp.deathFxFired = true;
            if (isEnemy)
            {
                sawEnemyDeath = true;
                normalKill    = true;
                if (m_world->HasComponent<BossActionComponent>(id))
                {
                    climax       = true;
                    bossDefeated = true;
                }
            }
        }
    });

    // 最後の1体を倒した（残りの敵が 0）ときも大きいスローにする
    if (sawEnemyDeath && aliveEnemies == 0) climax = true;

    if (climax)
    {
        Time::SlowWorld(*m_world, kClimaxScale, kClimaxDur);
        m_cooldown = kCooldown; // すぐ後に普通のスローが重ならないように
    }
    else if (normalKill && m_cooldown <= 0.0f)
    {
        Time::SlowWorld(*m_world, kNormalScale, kNormalDur);
        m_cooldown = kCooldown;
    }

    // --- 撃破ムービー ---

    // ボスを倒したスローは通常のカメラで見せ、スローが終わってから撃破ムービーを頼む。
    // ムービーの開始待ちはカメラを奪って最初の絵で止めるので、スローを見せる間には使えない。
    if (bossDefeated && !m_defeatMovieArmed)
    {
        m_defeatMovieArmed = true;
        m_defeatMovieTimer = kClimaxDur;
    }

    if (m_defeatMovieArmed && m_defeatMovieTimer > 0.0f)
    {
        m_defeatMovieTimer -= dt;
        if (m_defeatMovieTimer <= 0.0f)
            m_world->GetEventBus().Publish(CinematicPlayRequestEvent{kDefeatCinematic});
    }
}

// L06（ダメージの処理）で倒れたかが決まった後に見る
REGISTER_LOGIC_SYSTEM(DefeatSlowMotionSystem, Priority::LogicStage::L07_Cleanup);
