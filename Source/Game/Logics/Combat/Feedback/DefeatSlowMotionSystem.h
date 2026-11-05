#pragma once
// ===========================================================================
// 概要: 敵を倒した瞬間に、ゲーム全体を短い間スローにする System
//
// 責務:
//   - HP が 0 以下になった敵を1体につき1回だけ見つける（HealthComponent::deathFxFired）
//   - ボスを倒したとき・最後の敵を倒したときは、大きく長いスローにする
//   - 普通に倒したときは短いスローにし、続けて倒してもスローが続かないよう間を空ける
//   - ボスを倒したスローが終わったら、撃破ムービーの再生を頼む
//
// 備考: 倒れたエンティティは消えずに残るので、フラグがないと毎フレーム見つけてしまう
//       スローは Time::SlowWorld で TimeContext.cinematicSlow に書くだけ。TimeScaleSystem（L01）が
//       globalScale に掛け、ease-out で元の速さに戻す。globalScale は TimeStateComponent の
//       group に関係なく全部に効く。命中の瞬間のヒットストップは DamageResolutionSystem が行う
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

class DefeatSlowMotionSystem : public CCL::ECS::IfSystem<
    DefeatSlowMotionSystem,
    CCL::ECS::Write<HealthComponent>>
{
public:
    DefeatSlowMotionSystem() : IfSystem("DefeatSlowMotionSystem") {}

    void Update(float dt) override;

private:
    float m_cooldown         = 0.0f;  // 次の普通のスローを出せるまでの残り時間（実時間の秒）
    float m_defeatMovieTimer = 0.0f;  // 撃破ムービーを頼むまでの残り時間（実時間の秒）
    bool  m_defeatMovieArmed = false; // ボスを倒して、撃破ムービーを頼む待ちに入っているか

    // --- 調整値 ---
    static constexpr float kCooldown    = 0.6f;  // 普通のスローを出してから次に出せるまでの時間（秒）
    static constexpr float kNormalDur   = 0.30f; // 普通のスローの長さ（秒）
    static constexpr float kNormalScale = 0.35f; // 普通のスローの時間の速さ（0 で止まる、1 で等速）
    static constexpr float kClimaxDur   = 0.85f; // ボス・最後の敵を倒したときのスローの長さ（秒）
    static constexpr float kClimaxScale = 0.10f; // ボス・最後の敵を倒したときの時間の速さ
};
