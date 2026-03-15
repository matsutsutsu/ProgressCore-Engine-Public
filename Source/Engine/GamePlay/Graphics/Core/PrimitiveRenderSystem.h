#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Graphics/Core/PrimitiveComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"

class PrimitiveRenderSystem : public CCL::ECS::IfSystem<PrimitiveRenderSystem,
                                  CCL::ECS::Read<TransformComponent>,
                                  CCL::ECS::Read<PrimitiveComponent>> {
  public:
    PrimitiveRenderSystem();
    virtual ~PrimitiveRenderSystem() = default;

    void Update(float dt) override;
};