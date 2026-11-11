#pragma once
// ===========================================================================
// 概要: ボーン追従 Entity の描画用ワールド行列（renderWorldMatrix）を確定する System
//
// 責務:
//   - 親の補間済み renderWorldMatrix × ボーン行列 × 自分のオフセットで描画位置を決める
//   - 追従先の子孫へ描画用ワールド行列を伝播する
//   - Model を持つなら、確定した行列でノード行列を焼き直す
//
// 備考: ロジック側の BoneAttachmentSystem（当たり判定用の worldMatrix）とは責務を分ける
//       ボーン行列（globalTransform）はルート非依存なので両者で共用できる
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Transform/Components/BoneAttachmentComponent.h"
#include <DirectXMath.h>

// TransformInterpolationSystem（R00）で親の renderWorldMatrix が確定した後、
// ModelRenderSystem（R08）より前に実行すること
class BoneAttachmentRenderSystem : public CCL::ECS::IfSystem<
    BoneAttachmentRenderSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<BoneAttachmentComponent>>
{
public:
    BoneAttachmentRenderSystem() : IfSystem("BoneAttachmentRenderSystem") {}
    void Update(float dt) override;

private:
    // 追従先1つの描画用ワールド行列を、親の補間済みワールドとボーン行列から確定する
    void ResolveRenderAttachment(CCL::ECS::EntityID id, TransformComponent &trans, const BoneAttachmentComponent &attach);

    // 追従先の子孫へ描画用ワールド行列を伝播する
    void PropagateRenderSubtree(CCL::ECS::EntityID firstChild, const DirectX::XMFLOAT4X4 &parentRenderWorld);

    // Model を持つなら、確定した描画用ワールド行列でノード行列を焼き直す
    void BakeModelIfPresent(CCL::ECS::EntityID id, const DirectX::XMFLOAT4X4 &world);
};