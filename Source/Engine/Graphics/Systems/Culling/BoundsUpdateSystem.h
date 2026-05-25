#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/PrimitiveComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"

// ====================================================================
// Model用のAABB更新システム
// ====================================================================
class ModelBoundsUpdateSystem : public CCL::ECS::IfSystem<ModelBoundsUpdateSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<ModelComponent>,
    CCL::ECS::Write<BoundingBoxComponent>>
{
public:
    ModelBoundsUpdateSystem();
    virtual ~ModelBoundsUpdateSystem() = default;

    void Update(float dt) override;
};

// ====================================================================
// Primitive用のAABB更新システム
// ====================================================================
class PrimitiveBoundsUpdateSystem : public CCL::ECS::IfSystem<PrimitiveBoundsUpdateSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<PrimitiveComponent>,
    CCL::ECS::Write<BoundingBoxComponent>>
{
public:
    PrimitiveBoundsUpdateSystem();
    virtual ~PrimitiveBoundsUpdateSystem() = default;

    void Update(float dt) override;
};