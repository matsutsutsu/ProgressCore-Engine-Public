/// @file  DroneFormationSystem2.cpp
/// @brief IFormation 方式の陣形計算。registry から陣形を引いて targetPosition を計算。
#include "DroneFormationSystem.h"

#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace CCL::AI::Formation;

void DroneFormationSystem::Update(float dt)
{
    _elapsed += dt;

    auto &registry = FormationRegistry::Instance();

    // --- 場の情報を組み立てる（ボス位置・プレイヤー位置）---
    FormationContext ctx;
    ctx.time = _elapsed;

    // ボス位置（BossCommandComponent を持つ最初のEntity）
    {
        auto bossView = _world->View<BossCommandComponent, TransformComponent>();
        for (auto b : bossView)
        {
            if (auto *t = _world->GetComponent<TransformComponent>(b))
                ctx.bossPos = t->position;
            break;
        }
    }

    // プレイヤー位置（BattleContext から）
    if (_world->HasResource<BattleContext>())
    {
        auto &battle  = _world->GetResource<BattleContext>();
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