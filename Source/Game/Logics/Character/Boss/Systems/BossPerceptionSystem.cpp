// プレイヤーの探索は BattleContextSyncSystem が1回だけ行い、
// ここは BattleContext を読むだけ。
#include "BossPerceptionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Core/GameEvents.h" // DamageTakenEvent
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
    // 被弾量を blackboard へ足す。
    // イベントで受けるのは、Combat 側に AI の Component を知らせないため。
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &ev) {
        if (ev.isPlayer) return; // プレイヤーの被弾は敵 AI の判断に使わない

        if (auto *bt = m_world->GetComponent<BehaviorTreeComponent>(ev.targetID))
        {
            bt->blackboard.accumulatedDamage += ev.damageAmount;

            // 被弾でモーションは止めない。影響するのは次の技を選ぶ判断だけ。
            // 個体ごとの時間(localDt)で数えるので、
            // ヒットストップ中に「直近の被弾」が古くなることはない。
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
        // --- 被弾と HP ---

        // 残り時間ではなく経過時間で持つ。
        // どれだけ待つかは木（条件の param）が決めるため。
        btComp.blackboard.timeSinceLastHit += time.localDt;

        if (health.maxHealth > 0.0f)
        {
            btComp.blackboard.healthPercentage =
                (health.currentHealth / health.maxHealth) * 100.0f;

            // 蓄積した被弾を、最大 HP に対する割合に直す。
            // 足す側はダメージ量しか知らないので、
            // HealthComponent を読めるここで直せば、条件側は割合だけを見ればよい。

            // 割合で比べるのは、ダメージ量のまま比べると、
            // 最大 HP を変えたときに木のしきい値も直す必要があるため。
            btComp.blackboard.accumulatedDamageRatio =
                (btComp.blackboard.accumulatedDamage / health.maxHealth) * 100.0f;
        }

        // --- 狙う相手 ---

        // 狙う相手はここだけで決める。複数の敵を出すときは、
        // この1行を「最も近い相手を選ぶ」などに差し替えればよい。
        btComp.blackboard.targetEntity = battle.playerEntity;

        // 座標は BattleContext が集計済みなのでそこから取る
        // （全 AI が同じ Transform を読み直さないため）。
        // プレイヤー以外を狙うときだけ Transform を読む。
        Vector3 targetPos(battle.playerPos);
        if (btComp.blackboard.targetEntity != battle.playerEntity)
        {
            const auto *targetTrans =
                m_world->GetComponent<TransformComponent>(btComp.blackboard.targetEntity);
            if (!targetTrans) return; // ターゲットが消えている。今フレームは更新しない
            targetPos = Vector3(targetTrans->position);
        }

        // --- 相手との位置関係 ---

        btComp.blackboard.targetPosition   = targetPos;
        btComp.blackboard.distanceToTarget = Vector3::Distance(Vector3(trans.position), targetPos);

        // 距離だけだと「正面8m」と「背後8m」が同じ値になり、
        // 回り込まれたことが分からない。
        btComp.blackboard.angleToTarget = SignedAngleToTargetDeg(trans, targetPos);

        // 速度は BattleContext が持つ、平滑化済みのプレイヤー速度だけを使う。
        // 各 AI が前フレームの座標から速度を求めると、
        // 平滑化の強さが個体ごとにずれ、同じ状況で判断が分かれる。
        // プレイヤー以外を狙っているときは 0（近づいてきていない）にする。
        const Vector3 targetVelocity = (btComp.blackboard.targetEntity == battle.playerEntity)
                                           ? Vector3(battle.playerVelocity)
                                           : Vector3::Zero;

        btComp.blackboard.targetClosingSpeed =
            TargetClosingSpeed(Vector3(trans.position), targetPos, targetVelocity);

        // 近づく・離れるだけでは、横へ回り込む動きと止まっている状態の区別がつかない。
        // 技を相手の動き方で選び分けるために、横へ回る速さと全体の速さも持つ。
        btComp.blackboard.targetLateralSpeed =
            TargetLateralSpeed(Vector3(trans.position), targetPos, targetVelocity);
        btComp.blackboard.targetSpeed = Vector3(targetVelocity.x, 0.0f, targetVelocity.z).Length();

        // --- ステージ ---

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
    // 見上げ角は使わない。間合いの判断は真上から見た角度で足りる
    Vector3 toTarget = targetPos - Vector3(trans.position);
    toTarget.y       = 0.0f;

    // 完全に重なっていて角度が決まらない
    if (toTarget.LengthSquared() <= 0.0001f) return 0.0f;

    const Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);

    // yaw の差で測る。
    // 内積だと 0〜180 しか出ず、左右のどちらに回り込まれたかが消える。
    const float myYaw     = atan2f(forward.x, forward.z);
    const float targetYaw = atan2f(toTarget.x, toTarget.z);

    // -π〜π に収める
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
    // 自分の速度は含めない。含めると、逃げる相手を追いかけている
    // 自分の速度が加わって、相手が近づいてきていると誤判定する。
    // 知りたいのは相手の動きなので、相手の速度だけを使う。
    Vector3 targetToMe = myPos - targetPos;
    targetToMe.y       = 0.0f;
    if (targetToMe.LengthSquared() <= 0.0001f) return 0.0f;
    targetToMe.Normalize();

    Vector3 velocity = targetVelocity;
    velocity.y       = 0.0f; // ジャンプの上下を、近づいてきたと誤判定しないため

    return velocity.Dot(targetToMe);
}

float BossPerceptionSystem::TargetLateralSpeed(const Vector3 &myPos,
                                               const Vector3 &targetPos,
                                               const Vector3 &targetVelocity) const
{
    // 相手の速度から「自分へ近づく・離れる成分」を引いた残りが、横へ回る成分。
    // 自分の速度を含めない理由は TargetClosingSpeed と同じ
    Vector3 targetToMe = myPos - targetPos;
    targetToMe.y       = 0.0f;
    if (targetToMe.LengthSquared() <= 0.0001f) return 0.0f;
    targetToMe.Normalize();

    Vector3 velocity = targetVelocity;
    velocity.y       = 0.0f; // ジャンプの上下を横の動きに数えないため

    const Vector3 lateral = velocity - targetToMe * velocity.Dot(targetToMe);
    return lateral.Length();
}

float BossPerceptionSystem::DistanceToArenaEdge(const Vector3       &myWorldPos,
                                                const BattleContext &battle) const
{
    // 中心からの距離ではなく端までの距離を返す。
    // ステージの大きさを変えても、
    // 「端から 8m 以内」という木の書き方がそのまま使えるようにするため。
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

    // 中央に立っているか、相手と重なっているときは「塞がれていない」(180) を返す。
    // 0 を返すと、中央に居るのに追い込まれたと判定して逃げ出す。
    if (toCenter.LengthSquared() <= 0.0001f || toTarget.LengthSquared() <= 0.0001f) return 180.0f;

    toCenter.Normalize();
    toTarget.Normalize();

    // 左右は問わないので内積で足りる
    // （SignedAngleToTargetDeg と違い符号が要らない）。
    const float cosAngle = (std::max)(-1.0f, (std::min)(1.0f, toCenter.Dot(toTarget)));
    return DirectX::XMConvertToDegrees(std::acos(cosAngle));
}

// BehaviorTreeSystem(L02_Update) が blackboard を読む前に書き終える。
// 同じステージの BossPhaseSystem は GetRunAfter でこの後に走る。
REGISTER_LOGIC_SYSTEM(BossPerceptionSystem, Priority::LogicStage::L02_PreUpdate);
