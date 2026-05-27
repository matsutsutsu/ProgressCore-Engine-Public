#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Graphics/Components/Core/PrimitiveComponent.h"
#include "Engine/Graphics/Components//Culling/CullingComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

class PrimitiveRenderSystem : public CCL::ECS::IfSystem<PrimitiveRenderSystem,
                                  CCL::ECS::Read<TransformComponent>,
                                  CCL::ECS::Read<PrimitiveComponent>,
                                  CCL::ECS::Read<VisibilityComponent>> { 
  public:
    PrimitiveRenderSystem();
    virtual ~PrimitiveRenderSystem() = default;

    void Update(float dt) override;
};