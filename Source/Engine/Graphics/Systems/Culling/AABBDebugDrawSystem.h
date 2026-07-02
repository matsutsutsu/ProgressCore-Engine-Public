#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"

// カリング用のAABBを可視化するデバッグシステム
// ★修正：エンジンのECS文法に従い、CRTPと要求コンポーネントをテンプレートで明示
class AABBDebugDrawSystem : public CCL::ECS::IfSystem<AABBDebugDrawSystem,
    CCL::ECS::Read<BoundingBoxComponent>,
    CCL::ECS::Read<VisibilityComponent>>
{
public:
    AABBDebugDrawSystem();
    virtual ~AABBDebugDrawSystem() = default;

    void Update(float dt) override;
};