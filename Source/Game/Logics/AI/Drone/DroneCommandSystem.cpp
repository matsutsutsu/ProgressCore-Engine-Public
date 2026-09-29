#include "DroneCommandSystem.h"

#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h" // 陣形ごとの攻撃寿命
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void DroneCommandSystem::Update(float /*dt*/)
{
    // --- ボスの命令を1回だけ読む（1v1 なのでボスは1体）---
    BossCommandComponent *bossCmd = nullptr;

    auto bossView = m_world->View<BossCommandComponent>();
    for (auto b : bossView)
    {
        bossCmd = m_world->GetComponent<BossCommandComponent>(b);
        break; // 最初の1体だけ
    }
    if (!bossCmd) return;

    const DroneFormationType formation = bossCmd->requestFormation;
    const bool               attack    = bossCmd->droneAttackActive;

    // --- この陣形の攻撃を誰が終わらせるかを、フレームに1回だけ引く ---
    //   機体ごとに引かないので、仮想呼び出しはフレームあたり1回で済む。
    const auto *formationImpl = CCL::AI::Formation::FormationRegistry::Instance().Get(formation);
    const bool  individual =
        formationImpl &&
        formationImpl->GetLifecycle() == CCL::AI::Formation::ChargeLifecycle::Individual;

    // 「今撃て」信号の立ち上がり。出撃の号令はこの1フレームだけ有効にする。
    const bool rising = attack && !bossCmd->prevDroneAttackActive;

    // --- 全ドローンへ配布 ---
    ForEach([&](DroneComponent &drone) {
        // Hidden(=命令なし) のときは現在の陣形を維持する
        //   （スポーン直後の OrbitCircle を潰さない）。
        if (formation != DroneFormationType::Hidden)
            drone.currentFormation = formation;

        if (!individual)
        {
            // Broadcast: 信号が立っている間ずっと攻撃。
            //   陣形自体が動く技（縦割り・挟み込み）は全機が同時に動き切る必要があるため、
            //   個体に終了判断を持たせない。
            drone.wantsAttack = attack;
            drone.attackPhase = DroneAttackPhase::None;
            return;
        }

        // Individual: 出撃は一斉、終了は各機が自分で決める。
        if (rising)
        {
            drone.attackPhase = DroneAttackPhase::Charging;
            drone.chargeTimer = 0.0f;
        }
        else if (!attack && drone.attackPhase == DroneAttackPhase::Charging)
        {
            // ボスが信号を下ろした＝突撃の中断。
            //   既に離脱(Returning)へ移った機は巻き戻さない（帰り道は最後まで走らせる）。
            drone.attackPhase = DroneAttackPhase::None;
        }

        // wantsAttack は個体が持つフェーズから決める。信号をそのまま写さないので、
        // 飛び切った機が攻撃へ引き戻されない。
        drone.wantsAttack = (drone.attackPhase == DroneAttackPhase::Charging);
    });

    // 次フレームのエッジ検出用。必ず配布の後で更新する。
    bossCmd->prevDroneAttackActive = attack;
}

// ボスBT(L02_Update)が書いた命令を読み、DroneFormationSystem より前に配る。
// 同じステージ内の前後は GetRunAfter と、相手側の GetRunAfter で決まる。
REGISTER_LOGIC_SYSTEM(DroneCommandSystem, Priority::LogicStage::L02_Update)
