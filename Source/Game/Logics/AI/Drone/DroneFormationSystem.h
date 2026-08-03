/// @file  DroneFormationSystem2.h
/// @brief IFormation 方式の陣形計算システム（switch廃止版）。
///        各ドローンの currentFormation を見て registry から陣形を引き、
///        targetPosition を計算する。移動は既存 DroneMovementSystem が担当。
///
/// 旧 DroneFormationSystem との違い:
///   旧: switch(formationType) { case ...: CalculateXxx(); } が30個以上
///   新: registry.Get(type)->Calculate() の1本。陣形追加はクラス1個＋登録1行。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

/// @brief ドローンの目標座標を陣形に応じて計算するシステム。
class DroneFormationSystem : public CCL::ECS::IfSystem<
                                  DroneFormationSystem,
                                  CCL::ECS::Write<DroneComponent>>
{
  public:
    DroneFormationSystem()
        : IfSystem("DroneFormationSystem")
    {
    }

    void Initialize() override;

    void Update(float dt) override;

  private:
    float m_elapsed = 0.0f; // 回転アニメ用の経過時間
};