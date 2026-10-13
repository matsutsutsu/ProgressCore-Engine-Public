#pragma once
// ===========================================================================
// 概要: カリング用の AABB をワイヤーフレームで表示するデバッグ用 System。
//
// 責務:
//   - 見えているものは緑、カリングされたものは赤で AABB を ShapeRenderer に積む
//
// 備考: isDebugVisible が true のときだけ描く。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"

class AABBDebugDrawSystem : public CCL::ECS::IfSystem<AABBDebugDrawSystem,
                                                      CCL::ECS::Read<BoundingBoxComponent>,
                                                      CCL::ECS::Read<VisibilityComponent>>
{
public:
    AABBDebugDrawSystem() : IfSystem("AABBDebugDrawSystem") {}
    virtual ~AABBDebugDrawSystem() = default;

    void Update(float dt) override;
};
