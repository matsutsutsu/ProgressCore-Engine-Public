#pragma once
/// @file DefeatSlowMotionSystem.h
/// @brief 敵を撃破した瞬間に、世界全体を短時間スローにする「撃破スロー」演出システム。
///
/// 設計方針:
///   - 死亡は HealthComponent.currentHealth<=0 で判定。1体につき1回だけ発火するよう
///     HealthComponent.deathFxFired フラグで多重発火を防ぐ（死亡エンティティは破棄されず
///     残り続けるため、フラグが無いと毎フレーム再発火してしまう）。
///   - 演出は TimeContext.cinematicSlow（SlowMotionData）へ書き込むだけ。
///     TimeScaleSystem(L01) がこれを globalScale に折り込み、easeOut で通常速度へ戻す。
///     globalScale は group に依らず全 TimeStateComponent に効くので、スローが確実に見える。
///   - ボス撃破 or 最後の1体（ワールドクリア）は大きく長いスロー＝クライマックス。
///     通常撃破は短いスロー＋クールダウンで、連続撃破時のスロー連発を防ぐ。
///   - ヒットストップ（命中時の一瞬停止）は DamageResolutionSystem 側で実装済み。
///     本システムは「撃破の瞬間の余韻」だけを担当する（責務分離）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/HealthComponent.h"

class DefeatSlowMotionSystem
    : public CCL::ECS::IfSystem<DefeatSlowMotionSystem, CCL::ECS::Write<HealthComponent>>
{
  public:
    DefeatSlowMotionSystem() : IfSystem("DefeatSlowMotionSystem") {}

    void Update(float dt) override;

  private:
    float m_cooldown = 0.0f; ///< 通常撃破スローの連発防止（秒・実時間）

    // --- チューニング用パラメータ（ここを触れば手触りを調整できる） ---
    static constexpr float kCooldown    = 0.6f;  ///< 通常撃破スローの再発火間隔(秒)
    static constexpr float kNormalDur   = 0.30f; ///< 通常撃破スローの長さ(秒)
    static constexpr float kNormalScale = 0.35f; ///< 通常撃破スローの速度倍率(0で停止/1で等速)
    static constexpr float kClimaxDur   = 0.85f; ///< ボス/ワールドクリアの長さ(秒)
    static constexpr float kClimaxScale = 0.10f; ///< ボス/ワールドクリアの速度倍率
};
