// BoneAttachmentRenderSystem.h
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Transform/Components/BoneAttachmentComponent.h"
#include <DirectXMath.h>

/**
 * @brief ボーン追従エンティティの「描画専用」ワールド行列(renderWorldMatrix)を確定するシステム。
 * @details Logic側のBoneAttachmentSystem(当たり判定用worldMatrix)とは責務を分離する。
 *          こちらは親の補間済みrenderWorldMatrixを基準に、同じボーンglobalTransformを掛けて
 *          描画用の滑らかな追従を実現する。globalTransformはルート非依存のため共用できる。
 * @note TransformInterpolationSystem(R00)で親のrenderWorldMatrixが確定した後、
 *       ModelRenderSystem(R08)より前に実行すること。
 */
class BoneAttachmentRenderSystem : public CCL::ECS::IfSystem<BoneAttachmentRenderSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<BoneAttachmentComponent>> {
public:
    BoneAttachmentRenderSystem() : IfSystem("BoneAttachmentRenderSystem") {}
    void Update(float dt) override;

private:
    /// @brief 1つのattach先の描画用worldを、親の補間済みworldとボーンglobalから確定する。
    void ResolveRenderAttachment(CCL::ECS::EntityID id, TransformComponent& trans, const BoneAttachmentComponent& attach);

    /// @brief attach先のローカル小階層へ描画用worldを再伝播する。
    void PropagateRenderSubtree(CCL::ECS::EntityID firstChild, const DirectX::XMFLOAT4X4& parentRenderWorld);
};