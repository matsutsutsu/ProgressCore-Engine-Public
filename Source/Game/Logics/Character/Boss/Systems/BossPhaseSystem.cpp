#include "BossPhaseSystem.h"

#include "Game/Logics/AI/Drone/Components/DroneComponent.h" // DroneFormationType（ドローンへの指示）
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

        // 形態が変わったフレームだけ、入ったときの処理を行う。
        if (next != phase.phase)
        {
            OnEnterPhase(next, phase, bt, cmd, anim, health);
            phase.phase = next;
        }

        TickPhase(phase, cmd, health);

        // 時間で終わる形態（移行演出）のためのタイマー。
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
        // BT を完全に止める。assetPath を空にすると BehaviorTreeSystem が何もせずに戻り、
        // BossCommandComponent が上書きされないので、下の指示がそのまま効く。
        // sharedAsset は触らない（書くのは SyncAssets だけで、同じフレームのうちに null にする）。
        bt.assetPath.clear();

        // 攻撃を止めてドローンを頭上へ退避させる。Hidden は DroneCommandSystem が
        // 「命令なし」として無視するので使わない。
        cmd.droneAttackActive = false;
        cmd.currentActionId   = 0; // None → BossActionSystem は Idle
        cmd.requestFormation  = DroneFormationType::HighOrbit;

        // 演出中は無敵にする（TickPhase でも毎フレーム保つ）。
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
        // 死亡アニメはここで1回だけトリガーを引く。毎フレーム評価される BT では
        // 「1回だけ」を書きにくい（この System がある理由）。
        // 入ったときにも指示を消すのは、死んだ瞬間に攻撃の指示が1フレーム残らないようにするため。
        anim.SetTrigger(CCL::Utils::HashString(kDeathTrigger));

        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::Hidden;
        cmd.currentActionId   = 0;

        phase.timer = 0.0f;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] ボス死亡 → Dead へ");
        break;

    case BossPhase::Combat:
        // 初期状態。ほかの形態からここへ戻る経路は無い。
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
        // 演出中はボス本体もドローンも止めたまま、無敵を保つ。
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
        // 技は BT が選ぶ。
        break;
    }
}

// BehaviorTreeSystem(L02_Update) が assetPath を読む前に形態を決める。
REGISTER_LOGIC_SYSTEM(BossPhaseSystem, Priority::LogicStage::L02_PreUpdate)
