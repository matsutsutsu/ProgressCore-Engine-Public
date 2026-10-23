#include "DroneBrainSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h" // 突撃の始め方・切り上げを技に聞く
#include "Game/Logics/AI/Drone/Motion/DroneMotion.h"            // 突撃の始め方の既定・飛び出したか
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // ============================================================
    // 調整値
    // ============================================================

    constexpr float kBreakawayTime   = 1.6f; // 突き抜けた後に減速する時間 [秒]
    constexpr float kArriveThreshold = 2.5f; // 陣形スロットへ到着とみなす距離 [m]
} // namespace

void DroneBrainSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. プレイヤーの位置を読む
    // 2. 機体ごとに、優先度の高い順に状態を決める（突撃 → 離脱 → 接近 → 待機）

    // --- プレイヤーの位置 ---

    CCL::AI::Motion::ChargeBeginInfo chargeInfo{};
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();

        // リソースは常にあるので、プレイヤーの実在を見る。見ないと、居ないフレームに原点へ突撃する
        if (battle.HasPlayer())
        {
            chargeInfo.playerPos = Vector3(battle.playerPos);
            chargeInfo.hasPlayer = true;
        }
    }

    // 並列ループの外で1回だけ引く
    const auto *formations = CCL::AI::Formation::FormationRegistry::Find(*m_world);

    // --- 機体ごとの判断 ---

    // 優先度順に上から判定し、最初に成立したものが currentState になる。
    // 時間は各機の localDt（ヒットストップ中の機はタイマーも止まる）
    ForEachParallel([&](const TransformComponent &trans, DroneComponent &drone,
                        const TimeStateComponent &time) {
        const float dt = time.localDt;

        // --- 突撃 ---

        // 攻撃命令の流れ ボスBT → BossCommandComponent → DroneCommandSystem 
        if (drone.wantsAttack)
        {
            // 攻撃に入った瞬間に通る
            //  currentState がまだ FireCharge でない＝今フレームが突入フレーム。
            if (drone.currentState != DroneState::FireCharge)
            {
                // 技が見つからなければ、既定と同じ直進の始め方にする
                if (const auto *formation = formations ? formations->Get(drone.currentFormation) : nullptr)
                    formation->OnChargeBegin(drone, chargeInfo);
                else
                    CCL::AI::Motion::BeginStraightCharge(drone, chargeInfo);

                drone.chargeStartPos = Vector3(trans.position);
                drone.chargeTimer    = 0.0f;
            }

            // 小隊攻撃などの自分で終了を決めない攻撃が通る
            //  個別寿命の攻撃だけが自分で終了を決める（Broadcast はここを素通りする）。
            if (drone.attackPhase == DroneAttackPhase::Charging &&
                HasFinishedCharge(formations, trans, drone, dt))
            {
                drone.attackPhase    = DroneAttackPhase::Returning;
                drone.breakawayTimer = kBreakawayTime;
                drone.fireDirection  = Vector3::Zero;  // 次の突撃で方向を取り直させる
                drone.hasReachedSlot = false;          // 帰投後は並び直す
                drone.currentState   = DroneState::Returning;
                return;
            }

            drone.currentState = DroneState::FireCharge;
            return;
        }

        // --- 離脱 ---

        if (drone.attackPhase == DroneAttackPhase::Returning)
        {
            drone.breakawayTimer -= dt;
            if (drone.breakawayTimer > 0.0f)
            {
                drone.currentState = DroneState::Returning;
                return;
            }
            // 離脱を終えたら、下の接近が隊列へ連れ戻す
            drone.attackPhase = DroneAttackPhase::None;
        }

        // --- 接近 ---

        if (NeedsApproach(trans, drone))
        {
            drone.currentState = DroneState::MoveToTarget;
            return;
        }

        // --- 待機 ---

        drone.currentState = DroneState::Idle;
    });
}

bool DroneBrainSystem::HasFinishedCharge(const CCL::AI::Formation::FormationRegistry *formations,
                                         const TransformComponent                      &trans,
                                         DroneComponent                                &drone,
                                         float                                          dt) const
{
    // まだ飛び出していない間は、飛距離の起点を今の位置に置き直し続ける。
    // FireCharge へ入った瞬間に固定すると、弾幕は中央へ寄る移動まで飛距離に数え、届く前に打ち切る。

    // 当たらない区間＝まだ飛び出していない区間なので、動き方に聞けば済む
    if (!CCL::AI::Motion::IsChargeDangerous(drone))
    {
        drone.chargeStartPos = Vector3(trans.position);
        drone.chargeTimer    = 0.0f;
        return false;
    }

    drone.chargeTimer += dt;

    // 個別寿命の突撃は技が登録されているときしか始まらないので、見つからなければ続ける
    const auto *formation = formations ? formations->Get(drone.currentFormation) : nullptr;
    if (!formation) return false;

    const float flown = (Vector3(trans.position) - drone.chargeStartPos).Length();
    return formation->IsChargeOver(flown, drone.chargeTimer);
}

bool DroneBrainSystem::NeedsApproach(const TransformComponent &trans, DroneComponent &drone) const
{
    if (drone.hasReachedSlot) return false;

    const Vector3 cur(trans.position);
    const Vector3 target(drone.targetPosition);

    if (Vector3::Distance(cur, target) <= kArriveThreshold)
    {
        drone.hasReachedSlot = true;
        return false;
    }

    return true;
}

// 陣形（L02_Update）が今フレームの currentState を読めるよう手前に置く。ずらすと予備動作→突撃が1フレームずれる
REGISTER_LOGIC_SYSTEM(DroneBrainSystem, Priority::LogicStage::L02_PreUpdate)
