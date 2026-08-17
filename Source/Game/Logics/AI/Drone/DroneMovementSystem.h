/// @file  DroneMovementSystem.h
/// @brief 振付師が決定した目標座標へ向かってドローンを物理的に移動・回転させるシステム。
///
/// DroneState を Idle / MoveToTarget / FireCharge の3つに絞っている。
///   LockOn・Hold・HorizontalCharge・Shaking は演出の上乗せで、
///   「陣形を組む→隊列を直す→突撃する」という基礎ループには不要だった。
///   状態を増やすほど 1000機ぶんの分岐が増えるため、意図的に最小構成にしている。
///   追加が必要になっても「1ステート=1関数」の形は崩さない。
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
};