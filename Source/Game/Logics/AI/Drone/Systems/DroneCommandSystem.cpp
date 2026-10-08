#include "DroneCommandSystem.h"

#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h" // 陣形ごとの攻撃寿命
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void DroneCommandSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. ボスの命令を読む
    // 2. 指示された陣形の攻撃の寿命を引く
    // 3. 「今撃て」信号の立ち上がりを出す
    // 4. 全ドローンへ配る
    // 5. 次フレームのために信号を覚えておく

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

    // --- 攻撃の寿命 ---

    // この陣形の攻撃を誰が終わらせるか（Broadcast / Individual）を、フレームに1回だけ引く。
    // 機体ごとに引かないので、仮想呼び出しはフレームあたり1回で済む。
    const auto *formationImpl = CCL::AI::Formation::FormationRegistry::Instance().Get(formation);
    const bool  individual =
        formationImpl &&
        formationImpl->GetLifecycle() == CCL::AI::Formation::ChargeLifecycle::Individual;

    // --- 立ち上がり ---

    // 出撃の号令は、信号が立ったこの1フレームだけ有効にする。
    // Individual の陣形は個体ごとに状態を持つので、
    // 最初の１フレーム立ったかどうかだけを見て動く必要がある
    const bool rising = attack && !bossCmd->prevDroneAttackActive;


    ForEach([&](DroneComponent &drone) {
        // None（＝命令なし）のときは現在の陣形を維持する。
        // スポーン直後に Manager が入れた陣形（頭上円陣）を潰さないため。
        if (formation != DroneFormationType::None)
            drone.currentFormation = formation;

        // 自分のタイミングで攻撃を終わらせる（BloadCast）の場合通る
        if (!individual)
        {
            // Broadcast: 信号が立っている間ずっと攻撃。
            // 陣形自体が動く技（縦割り・挟み込み）は全機が同時に動き切る必要があるため、
            // 個体に終了判断を持たせない。
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
            drone.attackPhase = DroneAttackPhase::None;
        }

        // 既に離脱（Returning）へ移った機は、信号が下りても巻き戻さない。
        // 帰り道は最後まで走らせる。

        // wantsAttack は個体が持つフェーズから決める。
        // 信号をそのまま写さないので、飛び切った機が攻撃へ引き戻されない。
        drone.wantsAttack = (drone.attackPhase == DroneAttackPhase::Charging);
    });

    // --- 信号の記録 ---

    // 次フレームの立ち上がり検出に使う。
    // 今フレームの rising を求めた後で書く（先に書くと今フレームの立ち上がりが消える）。
    bossCmd->prevDroneAttackActive = attack;
}

// ボスBT(L02_Update)が書いた命令を読み、DroneFormationSystem より前に配る。
// 同じステージ内の前後は GetRunAfter と、相手側の GetRunAfter で決まる。
REGISTER_LOGIC_SYSTEM(DroneCommandSystem, Priority::LogicStage::L02_Update)
