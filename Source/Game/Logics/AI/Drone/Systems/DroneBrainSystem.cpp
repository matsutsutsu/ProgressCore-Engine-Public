#include "DroneBrainSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h" // 突撃の始め方・切り上げを技に聞く
#include "Game/Logics/AI/Drone/Motion/ChargeMotions.h"          // 技が見つからないときの始め方
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // ============================================================
    // 調整値（判断の優先度の順）
    // ============================================================

    // --- 突撃 ---

    // 突撃の始め方（タメ・撃つ方向）と切り上げ方は技ごとに違うので、ここには持たない
    // （IFormation::OnChargeBegin / IsChargeOver）。

    // --- 離脱 ---

    constexpr float kBreakawayTime = 1.6f; // 突き抜けた後に減速する時間 [秒]

    // --- 接近 ---

    constexpr float kArriveThreshold = 2.5f; // 陣形スロットへ到着とみなす距離 [m]
} // namespace

void DroneBrainSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. プレイヤーの位置を読む
    // 2. 機体ごとに、優先度の高い順に状態を決める（突撃 → 離脱 → 接近 → 待機）

    // --- プレイヤーの位置 ---

    // 突撃方向のフォールバックに使うプレイヤー座標は、毎フレーム1回だけ読む。
    CCL::AI::Motion::ChargeBeginInfo chargeInfo{};
    if (m_world->HasResource<BattleContext>())
    {
        const auto &battle = m_world->GetResource<BattleContext>();

        // リソースの有無ではなくプレイヤーの実在を見る。BattleContextSyncSystem が
        // 起動時に必ずリソースを作るので、ここを HasResource で済ませると
        // プレイヤーが居ないフレーム（タイトル・シーン遷移中）に playerPos の
        // 初期値=原点へ向かって突撃方向が決まる。
        if (battle.HasPlayer())
        {
            chargeInfo.playerPos = Vector3(battle.playerPos);
            chargeInfo.hasPlayer = true;
        }
    }

    // 突撃の始め方と切り上げ方を聞く技の一覧。並列のループの外で1回だけ引く。
    const auto *formations = CCL::AI::Formation::FormationRegistry::Find(*m_world);

    // --- 機体ごとの判断 ---

    // 優先度順に上から判定し、最初に成立したものが currentState になる。

    // 時間は各機の localDt を使う。
    // ヒットストップがかかった機は、タイマーも一緒に止まる。
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
                // タメや撃つ方向などの準備は技が決める（IFormation::OnChargeBegin）。
                // 技が見つからなければ、既定と同じ直進の始め方にする。
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

        // 突き抜けた後の離脱。FireCharge ではないので当たり判定の対象から外れる。
        if (drone.attackPhase == DroneAttackPhase::Returning)
        {
            drone.breakawayTimer -= dt;
            if (drone.breakawayTimer > 0.0f)
            {
                drone.currentState = DroneState::Returning;
                return;
            }
            // 以降は下の接近判定が拾って隊列へ連れ戻す。
            drone.attackPhase = DroneAttackPhase::None;
        }

        // --- 接近 ---

        // 持ち場へまだ一度も着いていない間だけ MoveToTarget で寄せる。
        if (NeedsApproach(trans, drone))
        {
            drone.currentState = DroneState::MoveToTarget;
            return;
        }

        // --- 待機 ---

        // 到着済み。Idle の「ゆらぎを伴う追従」がそのまま陣形を保つ挙動になる。
        drone.currentState = DroneState::Idle;
    });
}

bool DroneBrainSystem::HasFinishedCharge(const CCL::AI::Formation::FormationRegistry *formations,
                                         const TransformComponent                      &trans,
                                         DroneComponent                                &drone,
                                         float                                          dt) const
{
    // まだ飛び出していない間（発射待ち・タメ）は、飛距離の起点を今の位置に置き直し続ける。
    // こうすると技に渡す飛距離も経過時間も「飛び出してから」の量になる。

    // 起点を FireCharge へ入った瞬間のまま固定してはいけない。
    // 弾幕はそこから 順番待ち → 輪から中央へ移動（約21m）→ タメ と続いてから飛び出すので、
    // 「発射した時点で既に21m飛んだ」ことになり、届く前に打ち切る。
    // 症状としては「重なっているのに当たらない」になる。

    // 飛び出したかは、動き方に「当たり判定を開けてよいか」を聞いて見る。
    // 当たらない区間（直進なら発射待ちとタメ）＝まだ飛び出していない区間なので、
    // 発射待ちやタメの値をここで直接読まずに済む。
    if (!CCL::AI::Motion::GetChargeMotion(drone.chargeMode).IsDangerous(drone))
    {
        drone.chargeStartPos = Vector3(trans.position);
        drone.chargeTimer    = 0.0f;
        return false;
    }

    drone.chargeTimer += dt;

    // 切り上げるかは技に聞く。数値（届かせたい距離など）は技ごとに違うため。
    // 個別寿命の突撃は技が登録されているときしか始まらない（DroneCommandSystem）ので、見つからなければ続ける。
    const auto *formation = formations ? formations->Get(drone.currentFormation) : nullptr;
    if (!formation) return false;

    const float flown = (Vector3(trans.position) - drone.chargeStartPos).Length();
    return formation->IsChargeOver(flown, drone.chargeTimer);
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

// 陣形（L02_Update）が今フレームの currentState を読めるよう手前に置く。
REGISTER_LOGIC_SYSTEM(DroneBrainSystem, Priority::LogicStage::L02_PreUpdate)
