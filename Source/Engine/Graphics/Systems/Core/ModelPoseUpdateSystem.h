#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"


// モデルのノード行列やボーン行列を描画前に確定させるシステム
class ModelPoseUpdateSystem : public CCL::ECS::IfSystem<ModelPoseUpdateSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<ModelComponent>> {
public:
    ModelPoseUpdateSystem() : IfSystem("ModelPoseUpdateSystem") {}

    void Update(float dt) override;
};
