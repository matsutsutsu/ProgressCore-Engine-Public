/// @file  DroneCommandSystem.cpp
/// @brief ボス命令をドローンへ配布する実装。1v1 前提で最初のボスの命令を全機へ配る。
#include "DroneCommandSystem.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void DroneCommandSystem::Update(float /*dt*/)
{
    // --- ボスの命令を1回だけ読む（1v1 なのでボスは1体）---
    DroneFormationType formation = DroneFormationType::Hidden;
    bool               attack    = false;
    bool               hasBoss   = false;

    auto bossView = m_world->View<BossCommandComponent>();
    for (auto b : bossView)
    {
        if (auto *cmd = m_world->GetComponent<BossCommandComponent>(b))
        {
            formation = cmd->requestFormation;
            attack    = cmd->droneAttackActive;
            hasBoss   = true;
        }
        break; // 最初の1体だけ
    }
    if (!hasBoss) return;

    // --- 全ドローンへ配布 ---
    ForEach([&](DroneComponent &drone) {
        // Hidden(=命令なし) のときは現在の陣形を維持する
        //   （スポーン直後の OrbitCircle を潰さない）。
        if (formation != DroneFormationType::Hidden)
            drone.currentFormation = formation;

        drone.wantsAttack = attack;
    });
}

// 伝令はボスBT(L02_Update)が書いた命令を読み、Formation より前に配りたい。
// 同ステージ内の順序差は 1 フレーム程度で体感に影響しないため L02_Update に置く。
REGISTER_LOGIC_SYSTEM(DroneCommandSystem, Priority::LogicStage::L02_Update)
