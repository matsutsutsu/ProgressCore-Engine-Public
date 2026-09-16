#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
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

    // 実行順の宣言。ボーン追従の renderWorldMatrix が確定してから AABB を作る。
    //   本システムは描画と同じ renderWorldMatrix から AABB を組むため、
    //   BoneAttachmentRenderSystem（同じ R04 帯で renderWorldMatrix を書く）より
    //   後でなければ、剣などボーン追従モデルの AABB が 1 フレーム前の位置になる。
    //   同一ステージ内の順序はステージ値では決まらないので必ずここで宣言する。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BoneAttachmentRenderSystem"};
    }
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