/// @file  BossPhaseSystem.cpp
/// @brief ボス大状態の実装。遷移判定 → 入場処理（1回だけ）→ 滞在処理、の3段構成。
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
    ForEach([&](BossPhaseComponent      &phase,
                BehaviorTreeComponent   &bt,
                BossCommandComponent    &cmd,
                AnimParametersComponent &anim,
                HealthComponent         &health,
                const TimeStateComponent &time) {
        // --- 遷移判定 ---
        const BossPhase next = DecideNextPhase(phase, health);

        // --- 入場処理：状態が変わったフレームだけ走る ---
        if (next != phase.phase)
        {
            OnEnterPhase(next, phase, bt, cmd, anim, health);
            phase.phase = next;
        }

        // --- 滞在処理 ---
        TickPhase(phase, cmd, health);

        // 時間で終わる状態（移行演出）のためのタイマー
        if (phase.timer > 0.0f) phase.timer -= time.localDt;
    });
}


BossPhase BossPhaseSystem::DecideNextPhase(const BossPhaseComponent &phase,
                                           const HealthComponent    &health) const
{
    // 死は終端。一度入ったら他の状態へは戻さない。
    if (phase.phase == BossPhase::Dead) return BossPhase::Dead;

    // HP<=0 で死亡。DeadTag の付与はダメージと同フレームとは限らないため、
    // タグではなく HP を直接見る（HP0になった瞬間のフレームで確実に入る）。
    if (health.currentHealth <= 0.0f) return BossPhase::Dead;

    // 移行演出が終わったら第2形態へ
    if (phase.phase == BossPhase::Transition)
        return (phase.timer > 0.0f) ? BossPhase::Transition : BossPhase::Combat2;

    // 第1形態で HP が閾値を割ったら移行演出へ。
    //   閾値は BossPhaseComponent.phase2HealthThreshold（0〜100 のパーセント）。
    //   第2形態のBTが設定されていない場合は移行しない（設定漏れでボスが止まるのを防ぐ）。
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
        // BTを完全に止める。assetPath を空にし sharedAsset を手放すと
        // BehaviorTreeSystem が早期 return するので、この間は技が一切出ない。
        //   → BossCommandComponent が上書きされなくなり、下の指示がそのまま効く。
        bt.assetPath.clear();
        bt.sharedAsset.reset();

        // 攻撃を止めて群れを頭上へ退避させる。「仕切り直し」が画面で分かる状態にする。
        //   ※ Hidden は DroneCommandSystem が「命令なし」として無視するので使わない。
        cmd.droneAttackActive = false;
        cmd.currentActionId   = 0; // None → BossActionSystem は Idle
        cmd.requestFormation  = DroneFormationType::HighOrbit;

        // 演出中は無敵。滞在処理でも毎フレーム維持する。
        health.invincibilityTimer = kTransitionDuration;

        anim.SetTrigger(CCL::Utils::HashString(kRoarTrigger));
        phase.timer = kTransitionDuration;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] 第2形態へ移行開始（%.1f秒）", kTransitionDuration);
        break;

    case BossPhase::Combat2:
        // 第2形態のBTへ差し替える。assetPath が変わると BehaviorTreeSystem が自動ロードする。
        bt.assetPath = phase.phase2AssetPath;
        health.invincibilityTimer = 0.0f; // 無敵解除
        phase.timer               = 0.0f;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] 第2形態 開始 → %s", bt.assetPath.c_str());
        break;

    case BossPhase::Dead:
        // 死亡アニメはここで1回だけ発火する。毎フレーム評価される BT では
        // この「1回だけ」が素直に書けない（＝このシステムが存在する理由）。
        anim.SetTrigger(CCL::Utils::HashString(kDeathTrigger));

        // 入場した瞬間に命令を切る。滞在処理でも毎フレーム消すが、
        // 「死んだ瞬間に攻撃指示が1フレーム残る」のを防ぐため入場でも消しておく。
        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::Hidden;
        cmd.currentActionId   = 0;

        phase.timer = 0.0f;
        CCL_LOG_INFO(LogCategory::AI, "[BossPhase] ボス死亡 → Dead へ");
        break;

    case BossPhase::Combat:
        // 初期状態。ここへ「入場」してくる経路は今のところ無い。
        break;
    }
}


void BossPhaseSystem::TickPhase(const BossPhaseComponent &phase,
                                BossCommandComponent     &cmd,
                                HealthComponent          &health) const
{
    // 以前はここで blackboard.currentPhase（int）へ形態番号を写していた。
    //   BTの条件ノード（IsPhase2 / ShouldTransitionPhase2）が BossPhaseComponent を
    //   直接読むようになったので、写し替えそのものが不要になった。
    //   情報源が1つになるぶん、同期漏れでズレる余地が消える。

    switch (phase.phase)
    {
    case BossPhase::Transition:
        // 演出中はボス本体も群れも止めたまま、無敵を維持する
        //   （BTは止めてあるので、ここで書いた指示が上書きされない）。
        cmd.droneAttackActive = false;
        cmd.currentActionId   = 0;
        if (health.invincibilityTimer < phase.timer)
            health.invincibilityTimer = phase.timer;
        break;

    case BossPhase::Dead:
        // 群れへの指示を止め、本体の行動要求も止める（死んでいるのに攻撃が飛ぶのを防ぐ）。
        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::Hidden;
        cmd.currentActionId   = 0;
        break;

    case BossPhase::Combat:
    case BossPhase::Combat2:
        // 滞在中は何もしない。技の選択は BT の仕事。
        break;
    }
}

// L02_PreUpdate：BehaviorTreeSystem(L02_Update) が assetPath を読む前に大状態を確定させる
REGISTER_LOGIC_SYSTEM(BossPhaseSystem, Priority::LogicStage::L02_PreUpdate)
