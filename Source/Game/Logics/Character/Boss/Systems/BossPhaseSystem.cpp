/// @file  BossPhaseSystem.cpp
/// @brief ボス大状態の実装。遷移判定 → 入場処理（1回だけ）→ 滞在処理、の3段構成。
#include "BossPhaseSystem.h"

#include "Game/Logics/AI/Drone/DroneComponent.h" // DroneFormationType（死亡時の群れ収納）
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;

void BossPhaseSystem::Update(float /*rawDt*/)
{
    ForEach([&](BossPhaseComponent      &phase,
                BossCommandComponent    &cmd,
                AnimParametersComponent &anim,
                const HealthComponent   &health,
                const TimeStateComponent &time) {
        // --- 遷移判定 ---
        const BossPhase next = DecideNextPhase(phase, health);

        // --- 入場処理：状態が変わったフレームだけ走る ---
        if (next != phase.phase)
        {
            OnEnterPhase(next, phase, cmd, anim);
            phase.phase = next;
        }

        // --- 滞在処理 ---
        TickPhase(phase, cmd);

        // 時間で終わる状態（第2形態への移行演出など）のためのタイマー
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

    return BossPhase::Combat;
}


void BossPhaseSystem::OnEnterPhase(BossPhase                next,
                                   BossPhaseComponent      &phase,
                                   BossCommandComponent    &cmd,
                                   AnimParametersComponent &anim) const
{
    switch (next)
    {
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
        // 戦闘中に入場したときの特別な処理は今のところ無い。
        // BT は プレハブの BehaviorTreeComponent.assetPath が指すものが使われる。
        break;
    }
}


void BossPhaseSystem::TickPhase(const BossPhaseComponent &phase, BossCommandComponent &cmd) const
{
    switch (phase.phase)
    {
    case BossPhase::Dead:
        // 群れを収納し、本体の行動要求も止める（死んでいるのに攻撃が飛ぶのを防ぐ）。
        cmd.droneAttackActive = false;
        cmd.requestFormation  = DroneFormationType::Hidden;
        cmd.currentActionId   = 0;
        break;

    case BossPhase::Combat:
        // 滞在中は何もしない。技の選択は BT の仕事。
        break;
    }
}

// L02_PreUpdate：BehaviorTreeSystem(L02_Update) が assetPath を読む前に大状態を確定させる
REGISTER_LOGIC_SYSTEM(BossPhaseSystem, Priority::LogicStage::L02_PreUpdate)
