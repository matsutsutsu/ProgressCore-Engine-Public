/**
 * @file DroneMovementSystem.h
 * @brief 振付師が決定した目標座標へ向かってドローンを物理的に移動・回転させるシステム
 */
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/BehaviorTree/Drone/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"


class DroneMovementSystem : public CCL::ECS::IfSystem<
    DroneMovementSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<DroneComponent>,
    CCL::ECS::Read<TimeStateComponent> 
>
{
public:
    DroneMovementSystem() : IfSystem("DroneMovementSystem") {}
    virtual ~DroneMovementSystem() override = default;

    virtual void Update(float dt) override;

private:
    float m_currentTime = 0.0f; // システム稼働時間（震えの波の計算に必須）

private:
    // =========================================================
    // 物理ステートごとの処理を切り出したヘルパー関数群
    // (inline指定により、実行速度はベタ書きと完全に同等)
    // =========================================================

    /// @brief 滑らかに目標へ移動・回転する (Lerp/Slerp)
    inline void ProcessMoveToTarget(TransformComponent& trans, DroneComponent& drone, float dt);

    /// @brief 目標へ向かって直線的に高速突撃する
    inline void ProcessFireCharge(TransformComponent& trans, DroneComponent& drone, float dt);

    /// @brief スプリング制御、ホバーノイズ、バンキングを用いた有機的な追従
    inline void ProcessIdle(TransformComponent& trans, DroneComponent& drone, float dt);

    // 待機・エネルギー充填（発射前のブルブル震える演出）
    inline void ProcessLockOn(TransformComponent& trans, DroneComponent& drone, float dt);

    //  完全にその場に固定・停止する
    inline void ProcessHold(TransformComponent& trans, DroneComponent& drone, float dt);

    // 上に浮かず、そのまま少し後ろに引いてから直線突撃する
    inline void ProcessHorizontalCharge(TransformComponent& trans, DroneComponent& drone, float dt);

    /// @brief 目標座標へ追従しつつ、激しい振動ノイズを加算する
    inline void ProcessShaking(TransformComponent& trans, DroneComponent& drone, float dt, float currentTime); // ★追加
};