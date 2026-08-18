/// @file  DroneBrainSystem.cpp
/// @brief ドローン判断層の実装。優先度順に上から最初に成立したものが currentState になる。
#include "DroneBrainSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void DroneBrainSystem::Update(float /*dt*/)
{
    // 突撃方向のフォールバックに使うプレイヤー座標は、毎フレーム1回だけ読む。
    //   （旧 BeginChargeTask は突入した機体ごとにリソースを引いていた）
    Vector3 playerPos = Vector3::Zero;
    bool    hasPlayer = false;
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();
        playerPos = Vector3(battle.playerPos);
        hasPlayer = true;
    }

    // TimeStateComponent はビューに含めるだけで読まない。
    //   DroneMovementSystem と同一のアーキタイプを舐めるための一致であり、
    //   タイマーを判断側へ移すときにそのまま time.localDt を使えるようにしてある。
    ForEachParallel([&](const TransformComponent &trans, DroneComponent &drone,
                        const TimeStateComponent & /*time*/) {
        // ------------------------------------------------------------
        // 【最優先】攻撃命令が来ている
        //   ボスBT(DroneStrikeAction) → BossCommandComponent.droneAttackActive →
        //   DroneCommandSystem → drone.wantsAttack という経路で降りてくる。
        // ------------------------------------------------------------
        if (drone.wantsAttack)
        {
            // 「入った瞬間だけ」の初期化。currentState がまだ FireCharge でない
            //   ＝今フレームが突入フレーム、という判定で立ち上がりを1回に絞る。
            if (drone.currentState != DroneState::FireCharge)
            {
                BeginCharge(drone, playerPos, hasPlayer);
            }

            drone.currentState = DroneState::FireCharge;
            return;
        }

        // ------------------------------------------------------------
        // 陣形スロットへの初回接近（まだ持ち場に着いていない）
        // ------------------------------------------------------------
        if (NeedsApproach(trans, drone))
        {
            drone.currentState = DroneState::MoveToTarget;
            return;
        }

        // ------------------------------------------------------------
        // 到着済み：陣形維持
        //   Idle の「ゆらぎを伴う追従」がそのまま維持の挙動になるため、
        //   専用の状態は設けていない。
        // ------------------------------------------------------------
        drone.currentState = DroneState::Idle;
    });
}


bool DroneBrainSystem::NeedsApproach(const TransformComponent &trans, DroneComponent &drone) const
{
    // 一度到着したら、回転し続ける targetPosition との距離では接近へ戻さない
    if (drone.hasReachedSlot) return false;

    const Vector3 cur(trans.position);
    const Vector3 target(drone.targetPosition);

    if (Vector3::Distance(cur, target) <= kArriveThreshold)
    {
        drone.hasReachedSlot = true;   // 初回到着をラッチ
        return false;
    }

    return true;   // まだスロットへ向かう必要あり
}


void DroneBrainSystem::BeginCharge(DroneComponent &drone,
                                   const Vector3  &playerPos,
                                   bool            hasPlayer) const
{
    // 攻撃開始時のガード：直進モードから始める。
    //   陣形自体が動く攻撃（縦割り等）は Formation 側が毎フレーム true に上書きする。
    //   これにより前回の攻撃の追従モードが直進系の攻撃へ漏れない。
    drone.chargeFollowsTarget = false;

    // タメ時間。※追従モードでは ProcessFireCharge がタメを無視する。
    drone.stateTimer = kChargeWindup;

    // 突撃方向：陣形（GuillotineSlam 等）が予備動作中に既にセット済みならそれを尊重する
    if (drone.fireDirection.LengthSquared() >= 0.001f) return;

    Vector3 dir = -Vector3::UnitZ;
    if (hasPlayer)
    {
        Vector3 toPlayer = playerPos - drone.targetPosition;
        if (toPlayer.LengthSquared() > 0.001f)
        {
            toPlayer.Normalize();
            dir = toPlayer;
        }
    }

    drone.fireDirection       = dir;
    drone.targetLookDirection = dir;
}

// L02_PreUpdate：陣形（L02_Update）が今フレームの currentState を読めるよう手前に置く
REGISTER_LOGIC_SYSTEM(DroneBrainSystem, Priority::LogicStage::L02_PreUpdate)
