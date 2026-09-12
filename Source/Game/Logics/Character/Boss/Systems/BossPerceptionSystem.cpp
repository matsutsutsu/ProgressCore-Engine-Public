// ボスの状況認識の実装。BattleContext（世界の事実）を読んで blackboard（各ボスの
// 主観的認識）を更新する。プレイヤーの探索は BattleContext 側で1回だけ行われる。
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

void BossPerceptionSystem::Initialize()
{
    // 被弾量を blackboard へ積む。イベント購読にしているのは、Combat 側に
    // AI のコンポーネントを知らせないため。
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &ev) {
        if (ev.isPlayer) return; // プレイヤーの被弾は敵AIの判断材料ではない

        if (auto *bt = m_world->GetComponent<BehaviorTreeComponent>(ev.targetID))
        {
            bt->blackboard.accumulatedDamage += ev.damageAmount;

            // 被弾でモーションは止めない。止めるのは「次の技を選ぶ」判断だけ。
            // 個体の時計で数えるので、ヒットストップ中に様子見が明けることはない。
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

            // 蓄積した被弾を最大HPに対する割合へ直す。積む側は素の数値しか知らないので、
            // HealthComponent を引けるここで直すと、条件側は割合だけを見ればよくなる。
            btComp.blackboard.accumulatedDamageRatio =
                (btComp.blackboard.accumulatedDamage / health.maxHealth) * 100.0f;
        }

        // ここが「誰を狙うか」を決める唯一の場所。雑魚を群れで出すときは、この1行を
        // 「一番近い／一番脅威の相手を選ぶ」に差し替えるだけで済む。
        btComp.blackboard.targetEntity = battle.playerEntity;

        // 座標は BattleContext が集計済みなのでそこから取る（全AIが同じ Transform を
        // 引き直さないため）。プレイヤー以外を狙うようになったらそのときだけ引く。
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

        // 速度は BattleContext が平滑化済みのプレイヤー速度しか持たない。各AIが前フレーム
        // 座標を覚えて微分すると平滑化の強さが個体ごとにずれ、同じ状況で判断が割れる。
        // プレイヤー以外を狙っているときは 0 ＝「詰めてきていない」に倒す。
        const Vector3 targetVelocity = (btComp.blackboard.targetEntity == battle.playerEntity)
                                           ? Vector3(battle.playerVelocity)
                                           : Vector3::Zero;

        btComp.blackboard.targetClosingSpeed =
            TargetClosingSpeed(Vector3(trans.position), targetPos, targetVelocity);
    });
}


float BossPerceptionSystem::SignedAngleToTargetDeg(const TransformComponent &trans,
                                                   const Vector3            &targetPos) const
{
    Vector3 toTarget = targetPos - Vector3(trans.position);
    toTarget.y       = 0.0f;  // 見上げ角は落とす。間合いの判断は真上から見た角度で足りる
    if (toTarget.LengthSquared() <= 0.0001f) return 0.0f; // 完全に重なっている。前回値を汚さない

    const Vector3 forward = Vector3::Transform(Vector3::UnitZ, trans.rotation);

    // yaw の差で測る。内積だと 0〜180 しか出ず、左右のどちらに回り込まれたかが消える。
    const float myYaw     = atan2f(forward.x, forward.z);
    const float targetYaw = atan2f(toTarget.x, toTarget.z);

    float diff = targetYaw - myYaw;
    while (diff >  DirectX::XM_PI) diff -= DirectX::XM_2PI;
    while (diff < -DirectX::XM_PI) diff += DirectX::XM_2PI;

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
    velocity.y       = 0.0f;  // 跳躍の上下動を「詰めてきた」と読み違えないため

    return velocity.Dot(targetToMe);
}

// BehaviorTreeSystem(L02_Update) が blackboard を読む前に認識を確定させる。
// 同ステージの BossPhaseSystem / DroneBrainSystem は blackboard を読まないので
// 互いの順序は問わない。
REGISTER_LOGIC_SYSTEM(BossPerceptionSystem, Priority::LogicStage::L02_PreUpdate);