#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/PrimitiveComponent.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"

// Modelコンポーネントを持っているが、カリング用コンポーネントを持たないエンティティに
// 自動的に付与を行うシステム
class ModelCullingSetupSystem : public CCL::ECS::IfSystem<ModelCullingSetupSystem,
                                    CCL::ECS::Read<ModelComponent>>
{
  public:
    ModelCullingSetupSystem()
        : IfSystem("ModelCullingSetupSystem")
    {
        SetExclude<BoundingBoxComponent, VisibilityComponent>();
    }

    virtual ~ModelCullingSetupSystem() = default;

    void Update(float dt) override;
};

// Primitiveコンポーネントを持っているが、カリング用コンポーネントを持たないエンティティに
// 自動的に付与を行うシステム
class PrimitiveCullingSetupSystem : public CCL::ECS::IfSystem<PrimitiveCullingSetupSystem,
                                        CCL::ECS::Read<PrimitiveComponent>>
{
  public:
    PrimitiveCullingSetupSystem()
        : IfSystem("PrimitiveCullingSetupSystem")
    {
        SetExclude<BoundingBoxComponent, VisibilityComponent>();
    }

    virtual ~PrimitiveCullingSetupSystem() = default;

    void Update(float dt) override;
};