#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/AI/Navigation/Components/NavAgentComponent.h"

// 経路探索が必要なAIを見つけ出し、ウェイポイントを計算するシステム
class NavPathfindingSystem : public CCL::ECS::IfSystem<NavPathfindingSystem, 
                                  CCL::ECS::Read<TransformComponent>, 
                                  CCL::ECS::Write<NavAgentComponent>> {
public:
    NavPathfindingSystem() : IfSystem("NavPathfindingSystem") {}

	// virtual ~NavPathfindingSystem() override = default; // 基底クラスの仮想デストラクタを明示的にオーバーライド
    // 後で全てのシステムでやっておいた方が良い　それか後もうちょっと調べる
    virtual ~NavPathfindingSystem() = default;

    void Update(float dt) override;
};