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
    // 4. 全ドローンへ配る（陣形が変わる機は、前の陣形に後始末させてから）
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

    // 攻撃を誰が終わらせるか（Broadcast / Individual）。全機同じなのでフレームに1回だけ引く
    const auto *formations    = CCL::AI::Formation::FormationRegistry::Find(*m_world);
    const auto *formationImpl = formations ? formations->Get(formation) : nullptr;
    const bool  individual =
        formationImpl &&
        formationImpl->GetLifecycle() == CCL::AI::Formation::ChargeLifecycle::Individual;

    // --- 立ち上がり ---

    // 出撃の号令は、信号が立ったこの1フレームだけ有効にする。
    // Individual の陣形は個体ごとに状態を持つので、
    // 最初の１フレーム立ったかどうかだけを見て動く必要がある
    const bool rising = attack && !bossCmd->prevDroneAttackActive;


    ForEach([&](DroneComponent &drone) {
        // None（命令なし）のときは今の陣形を保つ。生成時に入れた頭上円陣を潰さないため
        if (formation != DroneFormationType::None && drone.currentFormation != formation)
        {
            // 抜ける陣形に、その技だけが使う値を片付けさせる
            if (const auto *prev = formations ? formations->Get(drone.currentFormation) : nullptr)
                prev->OnExit(drone);

            drone.currentFormation = formation;
        }

        // 自分のタイミングで攻撃を終わらせる（BloadCast）の場合通る
        if (!individual)
        {
            // Broadcast: 信号が立っている間ずっと攻撃
            drone.wantsAttack = attack;
            drone.attackPhase = DroneAttackPhase::None;
            return;
        }

        // Individual: 出撃は一斉、終了は各機が自分で決める
        if (rising)
        {
            drone.attackPhase = DroneAttackPhase::Charging;
            drone.chargeTimer = 0.0f;
        }
        else if (!attack && drone.attackPhase == DroneAttackPhase::Charging)
        {
            // 信号が下りた＝突撃の中断。離脱（Returning）へ移った機は巻き戻さず、帰り道を最後まで走らせる
            drone.attackPhase = DroneAttackPhase::None;
        }

        // 信号をそのまま写さず、個体のフェーズから決める。飛び切った機が攻撃へ引き戻されないように
        drone.wantsAttack = (drone.attackPhase == DroneAttackPhase::Charging);
    });

    // --- 信号の記録 ---

    // rising を求めた後で書く（先に書くと今フレームの立ち上がりが消える）
    bossCmd->prevDroneAttackActive = attack;
}

// ボスBT(L02_Update)が書いた命令を読み、DroneFormationSystem より前に配る
REGISTER_LOGIC_SYSTEM(DroneCommandSystem, Priority::LogicStage::L02_Update)
