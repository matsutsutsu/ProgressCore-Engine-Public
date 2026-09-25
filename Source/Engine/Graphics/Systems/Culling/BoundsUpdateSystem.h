#pragma once
// ===========================================================================
// 概要: Model と Primitive のワールド AABB を毎フレーム更新する System（R04_BoundsUpdate）。
//
// 責務:
//   - ModelBoundsUpdateSystem: モデルのローカル AABB の8頂点を renderWorldMatrix で変換して包む
//   - PrimitiveBoundsUpdateSystem: 形の寸法とスケールから、大まかな立方体の AABB を作る
//
// 備考: どちらも描画と同じ行列を基準にする（Model は renderWorldMatrix、Primitive は worldMatrix）。
//       カリング（R05）はこの AABB を使う。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/PrimitiveComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Culling/CullingComponents.h"

class ModelBoundsUpdateSystem : public CCL::ECS::IfSystem<ModelBoundsUpdateSystem,
                                                          CCL::ECS::Read<TransformComponent>,
                                                          CCL::ECS::Read<ModelComponent>,
                                                          CCL::ECS::Write<BoundingBoxComponent>>
{
public:
    ModelBoundsUpdateSystem();
    virtual ~ModelBoundsUpdateSystem() = default;

    void Update(float dt) override;

    // BoneAttachmentRenderSystem の後に走る。
    // このシステムは描画と同じ renderWorldMatrix から AABB を作るので、同じ R04 で
    // renderWorldMatrix を書く BoneAttachmentRenderSystem より後でないと、剣などボーンに
    // 付いたモデルの AABB が1フレーム前の位置になる。同じステージ内の順番はステージの値では
    // 決まらないので、ここで宣言する。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"BoneAttachmentRenderSystem"};
    }
};

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
