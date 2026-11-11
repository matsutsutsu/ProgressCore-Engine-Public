#pragma once
// ===========================================================================
// 概要: ボスを倒した瞬間にゲーム全体をスローにし、明けたら撃破ムービーを頼む System
//
// 責務:
//   - HP が 0 以下になったボスを1体につき1回だけ見つける（HealthComponent::deathFxFired）
//   - BossPhaseComponent の撃破演出の値で、スローをかける
//   - スローが終わったら、撃破ムービーの再生を頼む
//
// 備考: 倒れたエンティティは消えずに残るので、フラグがないと毎フレーム見つけてしまう
//       スローは Time::SlowWorld で TimeContext.cinematicSlow に書くだけ。TimeScaleSystem（L01）が
//       globalScale に掛け、ease-out で元の速さに戻す。globalScale は TimeStateComponent の
//       group に関係なく全部に効く。命中の瞬間のヒットストップは DamageResolutionSystem が行う
//       TODO(2026-11-10): 雑魚敵を倒したときのスローは、雑魚敵を作るときに足す
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"

class DefeatSlowMotionSystem : public CCL::ECS::IfSystem<
    DefeatSlowMotionSystem,
    CCL::ECS::Write<HealthComponent>,
    CCL::ECS::Read<BossPhaseComponent>>
{
public:
    DefeatSlowMotionSystem() : IfSystem("DefeatSlowMotionSystem") {}

    void Update(float dt) override;

private:
    float m_defeatMovieTimer = 0.0f;  // 撃破ムービーを頼むまでの残り時間（実時間の秒）
    bool  m_defeatMovieArmed = false; // ボスを倒して、撃破ムービーを頼む待ちに入っているか
};
