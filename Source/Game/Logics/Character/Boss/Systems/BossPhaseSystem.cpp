// ボス大状態の実装。遷移判定 → 入場処理（1回だけ）→ 滞在処理、の3段構成。
#include "BossPhaseSystem.h"

#include "Game/Logics/AI/Drone/DroneComponent.h" // DroneFormationType（群れへの指示）
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;

void BossPhaseSystem::Update(float /*rawDt*/)
{
    ForEach([&](BossPhaseComponent       &phase,
                BehaviorTreeComponent    &bt,
                BossCommandComponent     &cmd,
                AnimParametersComponent  &anim,
                HealthComponent          &health,
                const TimeStateComponent &time) {
        const BossPhase next = DecideNextPhase(phase, health);

        // 入場処理は状態が変わったフレームだけ走る。
        if (next != phase.phase)
        {
            OnEnterPhase(next, phase, bt, cmd, anim, health);
            phase.phase = next;
        }

        TickPhase(phase, cmd, health);

        // 時間で終わる状態（移行演出）のためのタイマー。
        if (phase.timer > 0.0f) phase.timer -= time.localDt;
    });
}

BossPhase BossPhaseSystem::DecideNextPhase(const BossPhaseComponent &phase,
                                           const HealthComponent    &health) const
{
    // 死は終端。一度入ったら戻さない。
    if (phase.phase == BossPhase::Dead) return BossPhase::Dead;

    // DeadTag の付与はダメージと同フレームとは限らないので、HP を直接見る。
    if (health.currentHealth <= 0.0f) return BossPhase::Dead;

    if (phase.phase == BossPhase::Transition)
        return (phase.timer > 0.0f) ? BossPhase::Transition : BossPhase::Combat2;

    // 第1形態で HP が閾値を割ったら移行演出へ。第2形態のBTが未設定なら移行しない
    // （設定漏れでボスが止まるのを防ぐ）。
    if (phase.phase == BossPhase::Combat && !phase.phase2AssetPath.empty() && health.maxHealth > 0.0f)
    {
        const float hpPercent = (health.currentHealth / health.maxHealth) * 100.0f;
        if (hpPercent <= phase.phase2HealthThreshold) return BossPhase::Transition;
    }

    return phase.phase;
}

void BossPhaseSystem::OnEnterPhase(BossPhase                next,
                                   BossPhaseComponent      &phase,
                                   BehaviorTreeComponent   &bt,
                                   BossCommandComponent    &cmd,
                                   AnimParametersComponent &anim,
                                   HealthComponent         &health) const
{
    switch (next)
    {
    case BossPhase::Transition:
        // BTを完全に止める。assetPath を空にすると BehaviorTreeSystem が早期 return し、
        // BossCommandComponent が上書きされないので下の指示がそのまま効く。
        // sharedAsset は触らない（書き手は SyncAssets だけ。同じフレームに null を配る）。
        bt.assetPath.clear();

        // 攻撃を止めて群れを頭上へ退避させる。Hidden は DroneCommandSystem が
        // 「命令なし」として無視するので使わない。
        cmd.droneAttackActive = false;
        cmd.currentActionId   = 0; // None → BossActionSystem は Idle
        cmd.requestFormation  = DroneFormationType::HighOrbit;

        // 演出中は無敵（滞在処理でも毎フレーム維持する）。
        health.invincibilityTimer = kTransitionDuration;

        anim.SetTrigger(CCL::Utils::HashString(kRoarTrigger));
        phase.timer = kTransitionDuration;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] 第2形態へ移行開始（%.1f秒）", kTransitionDuration);
        break;

    case BossPhase::Combat2:
        // assetPath が変わると BehaviorTreeSystem が自動ロードする。
        bt.assetPath              = phase.phase2AssetPath;
        health.invincibilityTimer = 0.0f; // 無敵解除
        phase.timer               = 0.0f;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] 第2形態 開始 → %s", bt.assetPath.c_str());
        break;

    case BossPhase::Dead:
        // 死亡アニメはここで1回だけ発火する。毎フレーム評価される BT では
        // この「1回だけ」が素直に書けない（＝このシステムが存在する理由）。
        // 入場でも命令を切るのは、死んだ瞬間に攻撃指示が1フレーム残るのを防ぐため。
        anim.SetTrigger(CCL::Utils::HashString(kDeathTrigger));

        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::Hidden;
        cmd.currentActionId   = 0;

        phase.timer = 0.0f;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] ボス死亡 → Dead へ");
        break;

    case BossPhase::Combat:
        // 初期状態。ここへ入場してくる経路は今のところ無い。
        break;
    }
}

void BossPhaseSystem::TickPhase(const BossPhaseComponent &phase,
                                BossCommandComponent     &cmd,
                                HealthComponent          &health) const
{

    switch (phase.phase)
    {
    case BossPhase::Transition:
        // 演出中はボス本体も群れも止めたまま、無敵を維持する。
        cmd.droneAttackActive = false;
        cmd.currentActionId   = 0;
        if (health.invincibilityTimer < phase.timer)
            health.invincibilityTimer = phase.timer;
        break;

    case BossPhase::Dead:
        // 死んでいるのに攻撃が飛ぶのを防ぐ。
        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::Hidden;
        cmd.currentActionId   = 0;
        break;

    case BossPhase::Combat:
    case BossPhase::Combat2:
        // 技の選択は BT の仕事。
        break;
    }
}

// BehaviorTreeSystem(L02_Update) が assetPath を読む前に大状態を確定させる。
REGISTER_LOGIC_SYSTEM(BossPhaseSystem, Priority::LogicStage::L02_PreUpdate)
