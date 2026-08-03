/// @file  DroneFormationSystem2.cpp
/// @brief IFormation 方式の陣形計算。registry から陣形を引いて targetPosition を計算。
#include "DroneFormationSystem.h"

#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent
#include "Game/Logics/AI/Drone/Formation/Formations_Basic.h"
#include "Game/Logics/AI/Drone/Formation/Formations_Attack.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace CCL::AI::Formation;

void DroneFormationSystem::Initialize()
{
    // このシステムが評価する陣形クラス（IFormation）を登録する。
    // 「DroneFormationSystem が動く」ということは「陣形クラスが必要」
    // ということなので、ここに書くのが最も自然。
    //
    // 陣形を追加した時（例：DeathRingFormation等）も、
    // Formations_Basic.h の RegisterAll() に1行足すだけで、
    // このシステムを持つ全シーンに自動反映される。
    CCL::AI::Formation::Basic::RegisterAll();
    CCL::AI::Formation::Attack::RegisterAll(); // 攻撃陣形（縦割り等）

    CCL_LOG_INFO(LogCategory::AI, "[DroneFormationSystem] 陣形クラス登録完了");
}

void DroneFormationSystem::Update(float dt)
{
    ZoneScopedN("DroneFormationSystem::Updateeeeeeeeeeeeeeee");

    m_elapsed += dt;

    auto &registry = FormationRegistry::Instance();

    // --- 場の情報を組み立てる（ボス位置・プレイヤー位置）---
    FormationContext ctx;
    ctx.time = m_elapsed;
    ctx.dt   = dt; // 攻撃の進行度（ヒンジ回転など）を進めるのに使う

    // ボス位置（BossCommandComponent を持つ最初のEntity）
    {
        auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
        for (auto b : bossView)
        {
            if (auto *t = m_world->GetComponent<TransformComponent>(b))
                ctx.bossPos = t->position;
            break;
        }
    }

    // プレイヤー位置（BattleContext から）
    if (m_world->HasResource<BattleContext>())
    {
        auto &battle  = m_world->GetResource<BattleContext>();
        ctx.playerPos = battle.playerPos;
    }

    // --- 各ドローンの目標座標を計算 ---
    //   ポリモーフィズムは「陣形を引くとき」だけ。ループ自体は連続アクセス。
    ForEach([&](DroneComponent &drone) {
        IFormation *formation = registry.Get(drone.currentFormation);
        if (!formation) return; // 未登録の陣形（Hidden等）は何もしない

        formation->Calculate(drone, ctx); // targetPosition を書き込む
    });
}

REGISTER_LOGIC_SYSTEM(DroneFormationSystem, Priority::LogicStage::L02_Update)