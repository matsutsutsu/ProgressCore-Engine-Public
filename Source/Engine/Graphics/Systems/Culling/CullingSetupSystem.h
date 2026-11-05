#pragma once
// ===========================================================================
// 概要: Model または Primitive を持つ Entity に、カリング用のコンポーネントを付ける System
//
// 責務:
//   - BoundingBoxComponent と VisibilityComponent の足りない方を付ける
//
// 備考: AddComponent は遅延され、次のロジック更新の先頭で反映される
//       そのため付けたフレームでは、まだカリングの対象にならない
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/PrimitiveComponent.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"

class ModelCullingSetupSystem : public CCL::ECS::IfSystem<
    ModelCullingSetupSystem,
    CCL::ECS::Read<ModelComponent>>
{
public:
    ModelCullingSetupSystem() : IfSystem("ModelCullingSetupSystem")
    {
        SetExclude<BoundingBoxComponent, VisibilityComponent>();
    }

    virtual ~ModelCullingSetupSystem() = default;

    void Update(float dt) override;
};

class PrimitiveCullingSetupSystem : public CCL::ECS::IfSystem<
    PrimitiveCullingSetupSystem,
    CCL::ECS::Read<PrimitiveComponent>>
{
public:
    PrimitiveCullingSetupSystem() : IfSystem("PrimitiveCullingSetupSystem")
    {
        SetExclude<BoundingBoxComponent, VisibilityComponent>();
    }

    virtual ~PrimitiveCullingSetupSystem() = default;

    void Update(float dt) override;
};
