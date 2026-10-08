#pragma once
// ===========================================================================
// 概要: PrimitiveComponent の形を PrimitiveRenderer に積む System（R08_Main）。
//
// 責務:
//   - 見えている Primitive だけを選ぶ
//   - 形ごとの寸法をスケール行列にして、ワールド行列に合成する
//   - 塗りつぶしかワイヤーフレームかで描画関数を選ぶ
//
// 備考: 基準は補間前の worldMatrix（PrimitiveBoundsUpdateSystem も同じ行列で AABB を作る）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Core/PrimitiveComponent.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

class PrimitiveRenderSystem : public CCL::ECS::IfSystem<PrimitiveRenderSystem,
                                                        CCL::ECS::Read<TransformComponent>,
                                                        CCL::ECS::Read<PrimitiveComponent>,
                                                        CCL::ECS::Read<VisibilityComponent>>
{
public:
    PrimitiveRenderSystem();
    virtual ~PrimitiveRenderSystem() = default;

    void Update(float dt) override;
};
