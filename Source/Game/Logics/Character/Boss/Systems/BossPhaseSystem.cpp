#include "BossPhaseSystem.h"

#include "Game/Logics/AI/Drone/Components/DroneComponent.h" // DroneFormationType（ドローンへの指示）
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/App/GameFlowState.h"         // 開幕ムービー中は序盤の上限時間を進めない
#include "Game/Logics/Tutorial/TutorialState.h" // 序盤（手加減）を続けるか

using namespace CCL::ECS;

void BossPhaseSystem::Update(float /*rawDt*/)
{
    // 序盤を続けるかはチュートリアルの進み具合で決める。TutorialState が無ければ序盤を設けない
    const TutorialState *tutorial = m_world->HasResource<TutorialState>() ? &m_world->GetResource<TutorialState>() : nullptr;
    const bool           tutorialRunning = tutorial && !tutorial->IsFinished();
    const bool cinematic =
        m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().cinematic;

    ForEach([&](BossPhaseComponent       &phase,
                BehaviorTreeComponent    &bt,
                BossCommandComponent     &cmd,
                AnimParametersComponent  &anim,
                HealthComponent          &health,
                const TimeStateComponent &time) {
        // チュートリアルがやり直された（エディタの「最初から開始」）。序盤に入り直せるようにする。
        // すでに序盤の途中なら、上限時間だけ数え直す
        if (tutorialRunning && phase.introGeneration != tutorial->generation)
        {
            phase.introGeneration = tutorial->generation;
            phase.introDone       = false;
            if (phase.phase == BossPhase::Intro) phase.timer = phase.introMaxDuration;
        }

        const BossPhase next = DecideNextPhase(phase, health, tutorialRunning);

        // 形態が変わったフレームだけ、入ったときの処理を行う。
        if (next != phase.phase)
        {
            OnEnterPhase(next, phase, bt, cmd, anim, health);
            phase.phase = next;
        }

        TickPhase(phase, cmd, health);

        // 時間で終わる形態（移行演出・序盤）のためのタイマー。
        // 序盤の上限は、開幕ムービーの間は進めない。ムービーが長いと、操作する前に序盤が終わってしまう
        const bool holdTimer = (phase.phase == BossPhase::Intro) && cinematic;
        if (phase.timer > 0.0f && !holdTimer) phase.timer -= time.localDt;
    });
}

BossPhase BossPhaseSystem::DecideNextPhase(const BossPhaseComponent &phase,
                                           const HealthComponent    &health,
                                           bool                      tutorialRunning) const
{
    // 死は終端。一度入ったら戻さない。
    if (phase.phase == BossPhase::Dead) return BossPhase::Dead;

    // DeadTag の付与はダメージと同フレームとは限らないので、HP を直接見る。
    if (health.currentHealth <= 0.0f) return BossPhase::Dead;

    if (phase.phase == BossPhase::Transition)
        return (phase.timer > 0.0f) ? BossPhase::Transition : BossPhase::Combat2;

    // 第1形態（序盤を含む）で HP が閾値を割ったら移行演出へ。第2形態のBTが未設定なら移行しない
    // （設定漏れでボスが止まるのを防ぐ）。序盤のうちに削り切られた場合も、序盤を飛ばしてそのまま移る。
    const bool firstForm = (phase.phase == BossPhase::Combat || phase.phase == BossPhase::Intro);
    if (firstForm && !phase.phase2AssetPath.empty() && health.maxHealth > 0.0f)
    {
        const float hpPercent = (health.currentHealth / health.maxHealth) * 100.0f;
        if (hpPercent <= phase.phase2HealthThreshold) return BossPhase::Transition;
    }

    // 序盤は、教え終えたか上限時間が来たら本来の戦いへ戻る
    if (phase.phase == BossPhase::Intro)
        return (!tutorialRunning || phase.timer <= 0.0f) ? BossPhase::Combat : BossPhase::Intro;

    // チュートリアルを行う挑戦なら、戦いの最初に序盤へ入る。一度終えたら入り直さない
    if (phase.phase == BossPhase::Combat && tutorialRunning && !phase.introDone && !phase.introAssetPath.empty())
        return BossPhase::Intro;

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

        // 攻撃を止めてドローンを頭上へ退避させる。None は DroneCommandSystem が
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
        cmd.requestFormation  = DroneFormationType::None;
        cmd.currentActionId   = 0;

        phase.timer = 0.0f;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] ボス死亡 → Dead へ");
        break;

    case BossPhase::Intro:
        // 本来の BT を覚えておき、序盤の BT へ差し替える。assetPath が変わると BehaviorTreeSystem が読み直す。
        phase.combatAssetPath = bt.assetPath;
        bt.assetPath          = phase.introAssetPath;
        phase.timer           = phase.introMaxDuration;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] 序盤（チュートリアル）開始 → %s", bt.assetPath.c_str());
        break;

    case BossPhase::Combat:
        // 初期状態のときはここを通らない（phase の初期値なので「入る」ことがない）。通るのは序盤から戻るときだけ。
        phase.introDone = true;
        phase.timer     = 0.0f;
        if (!phase.combatAssetPath.empty()) bt.assetPath = phase.combatAssetPath;

        // 序盤の技の途中で切り替わることがあるので、撃ちかけの指示を消して頭上へ戻す。
        // 残すと、本来の BT が最初の陣形を指示するまで、序盤の攻撃の続きが飛ぶ
        cmd.droneAttackActive = false;
        cmd.currentActionId   = 0;
        cmd.requestFormation  = DroneFormationType::HighOrbit;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] 序盤を終了 → %s", bt.assetPath.c_str());
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
        cmd.requestFormation  = DroneFormationType::None;
        cmd.currentActionId   = 0;
        break;

    case BossPhase::Combat:
    case BossPhase::Combat2:
    case BossPhase::Intro:
        // 技は BT が選ぶ。
        break;
    }
}

// BehaviorTreeSystem(L02_Update) が assetPath を読む前に形態を決める。
REGISTER_LOGIC_SYSTEM(BossPhaseSystem, Priority::LogicStage::L02_PreUpdate)
