#include "DroneCommandSystem.h"

#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h" // 抜ける陣形の後始末（OnExit）
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void DroneCommandSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. ボスの命令を読む
    // 2. 「今撃て」信号の立ち上がりを出す
    // 3. 全ドローンへ配る（陣形が変わる機は、前の陣形に後始末させてから）
    // 4. 次フレームのために信号を覚えておく

    // --- ボスの命令 ---

    // 1対1の戦闘なのでボスは1体。最初の1体の命令を1回だけ読む。
    BossCommandComponent *bossCmd = nullptr;

    auto bossView = m_world->View<BossCommandComponent>();
    for (auto b : bossView)
    {
        bossCmd = m_world->GetComponent<BossCommandComponent>(b);
        break;
    }
    if (!bossCmd) return;

    const DroneFormationType formation = bossCmd->requestFormation;
    const bool               attack    = bossCmd->droneAttackActive;

    // --- 立ち上がり ---

    // 出撃の号令は、信号が立ったこの1フレームだけ有効にする。
    // 各機は個体ごとに状態（wantsAttack）を持つので、
    // 最初の１フレーム立ったかどうかだけを見て動く必要がある
    const bool rising = attack && !bossCmd->prevDroneAttackActive;


    const auto *formations = CCL::AI::Formation::FormationRegistry::Find(*m_world);

    ForEach([&](DroneComponent &drone) {
        // None（命令なし）のときは今の陣形を保つ。生成時に入れた頭上円陣を潰さないため
        if (formation != DroneFormationType::None && drone.currentFormation != formation)
        {
            // 抜ける陣形に、その技だけが使う値を片付けさせる
            if (const auto *prev = formations ? formations->Get(drone.currentFormation) : nullptr)
                prev->OnExit(drone);

            drone.currentFormation = formation;
        }

        // 出撃は一斉。終わりは、技が IsChargeOver を書いていれば各機が自分で（Brain が下ろす）、
        // 書いていなければ信号が下りたとき。
        // 信号をそのまま写さないのは、自分で終えた機が次のフレームに攻撃へ引き戻されないため
        if (rising)
            drone.wantsAttack = true;
        else if (!attack)
            drone.wantsAttack = false;
    });

    // --- 信号の記録 ---

    // rising を求めた後で書く（先に書くと今フレームの立ち上がりが消える）
    bossCmd->prevDroneAttackActive = attack;
}

// ボスBT(L02_Update)が書いた命令を読み、DroneFormationSystem より前に配る
REGISTER_LOGIC_SYSTEM(DroneCommandSystem, Priority::LogicStage::L02_Update)
