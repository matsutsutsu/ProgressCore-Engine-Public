#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"

class NavMeshDebugDrawSystem : public CCL::ECS::IfSystem<NavMeshDebugDrawSystem> {
public:
    
    NavMeshDebugDrawSystem() : IfSystem("NavMeshDebugDrawSystem") {
        // システム生成時に最初からデバッグ描画をONにする
        this->isDebugVisible = true;
    }

    void Update(float dt) override;
};