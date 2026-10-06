// プレイヤーの探索は BattleContextSyncSystem が1回だけ行い、ここは BattleContext を読むだけ。
#include "BossPerceptionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/GameEvent/GameEvents.h" // DamageTakenEvent
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void BossPerceptionSystem::Initialize()
{
    // 被弾量を blackboard へ足す。イベントで受けるのは、Combat 側に
    // AI の Component を知らせないため。
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &ev) {
        if (ev.isPlayer) return; // プレイヤーの被弾は敵 AI の判断に使わない

        if (auto *bt = m_world->GetComponent<BehaviorTreeComponent>(ev.targetID))
        {
            bt->blackboard.accumulatedDamage += ev.damageAmount;

            // 被弾でモーションは止めない。影響するのは次の技を選ぶ判断だけ。
            // 個体ごとの時間(localDt)で数えるので、ヒットストップ中に様子見が終わることはない。
            bt->blackboard.timeSinceLastHit = 0.0f;
        }
    });
}

void BossPerceptionSystem::Update(float /*rawDt*/)
{
    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return; // プレイヤーがまだいなければ何もしない

    ForEach([&](BehaviorTreeComponent    &btComp,
                const TransformComponent &trans,
                const HealthComponent    &health,
                const TimeStateComponent &time) {
        btComp.blackboard.timeSinceLastHit += time.localDt;

        if (health.maxHealth > 0.0f)
        {
            btComp.blackboard.healthPercentage =
                (health.currentHealth / health.maxHealth) * 100.0f;

            // 蓄積した被弾を最大 HP に対する割合に直す。足す側はダメージ量しか知らないので、
            // HealthComponent を読めるここで直せば、条件側は割合だけを見ればよい。
            btComp.blackboard.accumulatedDamageRatio =
                (btComp.blackboard.accumulatedDamage / health.maxHealth) * 100.0f;
        }

        // 狙う相手はここだけで決める。複数の敵を出すときは、この1行を
        // 「最も近い相手を選ぶ」などに差し替えればよい。
        btComp.blackboard.targetEntity = battle.playerEntity;

        // 座標は BattleContext が集計済みなのでそこから取る（全 AI が同じ Transform を
        // 読み直さないため）。プレイヤー以外を狙うときだけ Transform を読む。
        Vector3 targetPos(battle.playerPos);
        if (btComp.blackboard.targetEntity != battle.playerEntity)
        {
            const auto *targetTrans =
                m_world->GetComponent<TransformComponent>(btComp.blackboard.targetEntity);
            if (!targetTrans) return; // ターゲットが消えている。今フレームは更新しない
            targetPos = Vector3(targetTrans->position);
        }

        btComp.blackboard.targetPosition   = targetPos;
        btComp.blackboard.distanceToTarget = Vector3::Distance(Vector3(trans.position), targetPos);
        btComp.blackboard.angleToTarget    = SignedAngleToTargetDeg(trans, targetPos);

        // 速度は BattleContext が持つ平滑化済みのプレイヤー速度だけを使う。各 AI が前フレームの
        // 座標から速度を求めると、平滑化の強さが個体ごとにずれ、同じ状況で判断が分かれる。
        // プレイヤー以外を狙っているときは 0（近づいてきていない）にする。
        const Vector3 targetVelocity = (btComp.blackboard.targetEntity == battle.playerEntity)
                                           ? Vector3(battle.playerVelocity)
                                           : Vector3::Zero;

        btComp.blackboard.targetClosingSpeed =
            TargetClosingSpeed(Vector3(trans.position), targetPos, targetVelocity);

        const Vector3 myWorldPos(trans.GetWorldPosition());
        btComp.blackboard.distanceToArenaEdge   = DistanceToArenaEdge(myWorldPos, battle);
        btComp.blackboard.targetAngleFromCenter = TargetAngleFromCenterDeg(myWorldPos, targetPos, battle);
    });
}

// ---------------------------------------------------------------------------
// 幾何の計算
// ---------------------------------------------------------------------------

float BossPerceptionSystem::SignedAngleToTargetDeg(const TransformComponent &trans,
                                                   const Vector3            &targetPos) const
{
    Vector3 toTarget = targetPos - Vector3(trans.position);
    toTarget.y       = 0.0f;                              // 見上げ角は使わない。間合いの判断は真上から見た角度で足りる
    if (toTarget.LengthSquared() <= 0.0001f) return 0.0f; // 完全に重なっていて角度が決まらない

    const Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);

    // yaw の差で測る。内積だと 0〜180 しか出ず、左右のどちらに回り込まれたかが消える。
    const float myYaw     = atan2f(forward.x, forward.z);
    const float targetYaw = atan2f(toTarget.x, toTarget.z);

    float diff = targetYaw - myYaw;
    while (diff > DirectX::XM_PI)
        diff -= DirectX::XM_2PI;
    while (diff < -DirectX::XM_PI)
        diff += DirectX::XM_2PI;

    return DirectX::XMConvertToDegrees(diff);
}

float BossPerceptionSystem::TargetClosingSpeed(const Vector3 &myPos,
                                               const Vector3 &targetPos,
                                               const Vector3 &targetVelocity) const
{
    Vector3 targetToMe = myPos - targetPos;
    targetToMe.y       = 0.0f;
    if (targetToMe.LengthSquared() <= 0.0001f) return 0.0f;
    targetToMe.Normalize();

    Vector3 velocity = targetVelocity;
    velocity.y       = 0.0f; // ジャンプの上下の動きを、近づいてきたと誤判定しないため

    return velocity.Dot(targetToMe);
}

float BossPerceptionSystem::DistanceToArenaEdge(const Vector3       &myWorldPos,
                                                const BattleContext &battle) const
{
    Vector3 fromCenter = myWorldPos - Vector3(battle.arenaCenter);
    fromCenter.y       = 0.0f; // 高さの違いで端までの距離が変わらないようにする

    return battle.arenaRadius - fromCenter.Length();
}

float BossPerceptionSystem::TargetAngleFromCenterDeg(const Vector3       &myWorldPos,
                                                     const Vector3       &targetPos,
                                                     const BattleContext &battle) const
{
    Vector3 toCenter = Vector3(battle.arenaCenter) - myWorldPos;
    Vector3 toTarget = targetPos - myWorldPos;
    toCenter.y       = 0.0f;
    toTarget.y       = 0.0f;

    // 中央に立っているか相手と重なっているときは「塞がれていない」(180) を返す。
    // 0 を返すと、中央に居るのに追い込まれたと判定して逃げ出す。
    if (toCenter.LengthSquared() <= 0.0001f || toTarget.LengthSquared() <= 0.0001f) return 180.0f;

    toCenter.Normalize();
    toTarget.Normalize();

    // 左右は問わないので内積で足りる（SignedAngleToTargetDeg と違い符号が要らない）。
    const float cosAngle = (std::max)(-1.0f, (std::min)(1.0f, toCenter.Dot(toTarget)));
    return DirectX::XMConvertToDegrees(std::acos(cosAngle));
}

// BehaviorTreeSystem(L02_Update) が blackboard を読む前に書き終える。
// 同じステージの BossPhaseSystem は GetRunAfter でこの後に走る。
REGISTER_LOGIC_SYSTEM(BossPerceptionSystem, Priority::LogicStage::L02_PreUpdate);
