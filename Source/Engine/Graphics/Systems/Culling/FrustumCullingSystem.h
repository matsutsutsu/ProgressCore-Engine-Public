#pragma once
// ===========================================================================
// 概要: AABB とカメラの視錐台を判定し、VisibilityComponent を書き換える System（R05_Culling）。
//
// 責務:
//   - 全 Entity の worldAABB が視錐台と交わるかを並列に判定し、isVisible に書く
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"

class FrustumCullingSystem : public CCL::ECS::IfSystem<FrustumCullingSystem,
                                                       CCL::ECS::Read<BoundingBoxComponent>,
                                                       CCL::ECS::Write<VisibilityComponent>>
{
public:
    FrustumCullingSystem();
    virtual ~FrustumCullingSystem() = default;

    void Update(float dt) override;
};
