// ドローン判断層の実装。優先度順に上から最初に成立したものが currentState になる。
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
    Vector3 playerPos = Vector3::Zero;
    bool    hasPlayer = false;
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();

        // リソースの有無ではなくプレイヤーの実在を見る。BattleContextSyncSystem が
        // 起動時に必ずリソースを作るので、ここを HasResource で済ませると
        // プレイヤーが居ないフレーム（タイトル・シーン遷移中）に playerPos の
        // 初期値=原点へ向かって突撃方向が決まる。
        if (battle.HasPlayer())
        {
            playerPos = Vector3(battle.playerPos);
            hasPlayer = true;
        }
    }

    // 各機の時計を使う（被弾でヒットストップがかかった機はタイマーも止まる）。
    ForEachParallel([&](const TransformComponent &trans, DroneComponent &drone,
                        const TimeStateComponent &time) {
        const float dt = time.localDt;

        // 最優先。押しのけられている間は隊列にも攻撃にも戻らない。
        if (UpdateStun(drone, dt))
        {
            drone.currentState = DroneState::Stunned;
            return;
        }

        // 攻撃命令。ボスBT → BossCommandComponent → DroneCommandSystem と降りてくる。
        if (drone.wantsAttack)
        {
            // currentState がまだ FireCharge でない＝今フレームが突入フレーム。
            if (drone.currentState != DroneState::FireCharge)
            {
                BeginCharge(drone, playerPos, hasPlayer);
                drone.chargeStartPos = Vector3(trans.position);
                drone.chargeTimer    = 0.0f;
            }

            // 個別寿命の攻撃だけが自分で終了を決める（Broadcast はここを素通りする）。
            if (drone.attackPhase == DroneAttackPhase::Charging &&
                HasFinishedCharge(trans, drone, dt))
            {
                drone.attackPhase    = DroneAttackPhase::Returning;
                drone.stateTimer     = kBreakawayTime; // 離脱カーブの長さとして持ち替える
                drone.fireDirection  = Vector3::Zero;  // 次の突撃で方向を取り直させる
                drone.hasReachedSlot = false;          // 帰投後は並び直す
                drone.currentState   = DroneState::Returning;
                return;
            }

            drone.currentState = DroneState::FireCharge;
            return;
        }

        // 突き抜けた後の離脱。FireCharge ではないので当たり判定の対象から外れる。
        if (drone.attackPhase == DroneAttackPhase::Returning)
        {
            drone.stateTimer -= dt;
            if (drone.stateTimer > 0.0f)
            {
                drone.currentState = DroneState::Returning;
                return;
            }
            // 以降は下の接近判定が拾って隊列へ連れ戻す。
            drone.attackPhase = DroneAttackPhase::None;
        }

        // 陣形スロットへの初回接近。
        if (NeedsApproach(trans, drone))
        {
            drone.currentState = DroneState::MoveToTarget;
            return;
        }

        // 到着済み。Idle の「ゆらぎを伴う追従」がそのまま維持の挙動になる。
        drone.currentState = DroneState::Idle;
    });
}

bool DroneBrainSystem::UpdateStun(DroneComponent &drone, float dt) const
{
    // 吹き飛ばし中。
    if (drone.knockbackTimer > 0.0f)
    {
        drone.knockbackTimer -= dt;
        if (drone.knockbackTimer > 0.0f) return true;

        // 収まった瞬間に「立て直し」へ引き継ぐ。
        drone.knockbackTimer = 0.0f;
        drone.recoverTimer   = kRecoverBase + (drone.localIndex % 5) * kRecoverStagger;

        // 押しのけられた時点で突撃は成立しない。
        drone.attackPhase   = DroneAttackPhase::None;
        drone.fireDirection = Vector3::Zero;

        // Idle のゆるいバネではなく MoveToTarget で機敏に戻す。
        // 「自分で判断して持ち場に帰った」ように見せるための1行。
        drone.hasReachedSlot = false;
        return true;
    }

    // 立て直し中（その場で姿勢を戻すだけ）。
    if (drone.recoverTimer > 0.0f)
    {
        drone.recoverTimer -= dt;
        if (drone.recoverTimer < 0.0f) drone.recoverTimer = 0.0f;
        return true;
    }

    return false;
}

bool DroneBrainSystem::HasFinishedCharge(const TransformComponent &trans,
                                         DroneComponent           &drone,
                                         float                     dt) const
{
    // まだ飛び出していない間は、飛距離の起点を今の位置に置き直し続ける。
    //   起点は FireCharge へ入った瞬間に記録されるが、弾幕はそこから順番待ち →
    //   輪から中央へ移動（約21m）→ タメ と続いてから飛び出す。置き直さないと
    //   「発射した時点で既に21m飛んだ」ことになり、上限に早く到達して届く前に
    //   打ち切る＝「重なっているのに当たらない」形で表に出る。
    //   ここで置き直せば kChargeRange も kChargeTimeout も「飛び出してから」になる。
    if (drone.fireDelay > 0.0f || drone.stateTimer > 0.0f)
    {
        drone.chargeStartPos = Vector3(trans.position);
        drone.chargeTimer    = 0.0f;
        return false;
    }

    drone.chargeTimer += dt;

    const float flownSq = (Vector3(trans.position) - drone.chargeStartPos).LengthSquared();
    if (flownSq > kChargeRange * kChargeRange) return true;

    return drone.chargeTimer > kChargeTimeout;
}

bool DroneBrainSystem::NeedsApproach(const TransformComponent &trans, DroneComponent &drone) const
{
    // 一度到着したら、回転し続ける targetPosition との距離では接近へ戻さない。
    if (drone.hasReachedSlot) return false;

    const Vector3 cur(trans.position);
    const Vector3 target(drone.targetPosition);

    if (Vector3::Distance(cur, target) <= kArriveThreshold)
    {
        drone.hasReachedSlot = true; // 初回到着をラッチ
        return false;
    }

    return true; // まだスロットへ向かう必要あり
}

void DroneBrainSystem::BeginCharge(DroneComponent &drone,
                                   const Vector3  &playerPos,
                                   bool            hasPlayer) const
{
    // 直進モードから始める。陣形自体が動く攻撃は Formation 側が毎フレーム上書きするので、
    // 前回の攻撃の追従モードが直進系の攻撃へ漏れない。
    drone.chargeMode = ChargeMode::Straight;

    // タメ時間。追従モードでは ProcessFireCharge がこれを無視する。
    drone.stateTimer = kChargeWindup;

    // 陣形が予備動作中に既にセット済みならそれを尊重する。
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

// 陣形（L02_Update）が今フレームの currentState を読めるよう手前に置く。
REGISTER_LOGIC_SYSTEM(DroneBrainSystem, Priority::LogicStage::L02_PreUpdate)
