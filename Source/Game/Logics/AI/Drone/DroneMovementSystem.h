/// @file  DroneMovementSystem.h
/// @brief 振付師が決定した目標座標へ向かってドローンを物理的に移動・回転させるシステム。
///
/// 状態は「基礎ループ3つ」＋「個体が自分で入る2つ」で構成する。
///   Idle / MoveToTarget / FireCharge … 陣形を組む→隊列を直す→突撃する（ボスの指示で動く）
///   Stunned / Returning              … 吹き飛ばされた／飛び切った（個体が自分で入る）
///   LockOn・Hold・HorizontalCharge・Shaking は演出の上乗せなので作らない。
///   状態を増やすほど 1000機ぶんの分岐が増えるため、増やすのは
///   「見た目の動きが根本的に違うもの」だけに絞り、「1ステート=1関数」の形は崩さない。
///
/// 実行順序: L03_PrePhysics（PushSystem より前）で trans を確定させる。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"

class DroneMovementSystem : public CCL::ECS::IfSystem<
                                DroneMovementSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Write<DroneComponent>,
                                CCL::ECS::Read<TimeStateComponent>>
{
  public:
    DroneMovementSystem()
        : IfSystem("DroneMovementSystem")
    {
    }
    virtual ~DroneMovementSystem() override = default;

    virtual void Update(float dt) override;

  private:
    // =========================================================
    // 各関数は drone.currentVelocity（意思）と rotation を計算する。
    // 最後に trans.position へ積分するのは Update 側の共通処理が担う。
    // =========================================================

    /// @brief 滑らかに目標へ移動・回転する (Lerp/Slerp)。陣形の再構築に使う。
    inline void ProcessMoveToTarget(TransformComponent &trans, DroneComponent &drone, float dt);

    /// @brief 陣形を維持しつつフワフワ追従する。待機・巡回に使う。
    inline void ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt);

    /// @brief 目標方向へ直線的に高速突撃する。ジャスト回避の対象となる攻撃状態。
    inline void ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt);

    /// @brief 隊列を組んだままの突入。targetPosition へバネで追従する。
    /// @note  剛体追従と違って形が歪む。歪むことがこの技の見た目の核。
    inline void ProcessChargeSoftFollow(TransformComponent &trans, DroneComponent &drone, float dt);

    /// @brief 切り払いで吹き飛ばされ、制御を失って漂う。通常の追従・突撃を一切行わない。
    /// @note  タイマーの管理は DroneBrainSystem 側。ここは「漂って減衰する」動きだけを持つ。
    inline void ProcessStunned(TransformComponent &trans, DroneComponent &drone, float dt);

    /// @brief 突き抜けた後、上方へ抜けながら減速する離脱カーブ。
    /// @note  その場で急旋回させると突撃の勢いが嘘になるため、一度抜けさせてから
    ///        MoveToTarget に隊列復帰を任せる。
    inline void ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt);

    /// @brief 吹き飛ばし速度の減衰率（1/秒）。数フレームで初速→ほぼ0にする。
    static constexpr float kKnockbackDecay = 6.0f;

    /// @brief 離脱中に上方向へ加える加速度。突撃の軌道から「抜ける」ための持ち上げ。
    static constexpr float kBreakawayLift = 18.0f;

    /// @brief 離脱中の減速率（1/秒）。突撃速度を帰投できる速さまで落とす。
    static constexpr float kBreakawayDrag = 2.5f;

    /// @brief 同じく速度の追従の滑らかさ。
    static constexpr float kSoftFollowDamping = 10.0f;

    /// @brief 隊列追従時の最高速度倍率。頭の突入速度を上回る必要がある。
    static constexpr float kSoftFollowMaxSpeedScale = 2.4f;

    /// @brief slotInSquad から追従の鈍さ係数を出す。
    /// @note  後方の機ほどワンテンポ遅れて曲がるので、隊列がムチのようにしなる。
    ///        「編隊で飛んでいる」感の大半はこの遅れから出ている。
    ///        小隊に属さない機は slotInSquad=0 なので影響を受けない。
    float SlotLag(const DroneComponent &drone) const
    {
        const float lag = 1.0f - static_cast<float>(drone.slotInSquad) * m_slotLagPerSlot;
        return (lag < 0.15f) ? 0.15f : lag;   // 遅すぎて隊列から千切れないよう下限を置く
    }

    // --- SquadTuning から毎フレーム写し取る（ImGui で実行中に変えられる）---
    //   並列ループから resource を引かずに済むよう、Update の先頭で1回だけコピーする。
    float m_followGain     = 10.0f;
    float m_slotLagPerSlot = 0.06f;
};