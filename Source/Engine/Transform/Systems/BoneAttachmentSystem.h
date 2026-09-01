#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Transform/Components/BoneAttachmentComponent.h"
#include <DirectXMath.h>

/**
 * @brief 親エンティティのボーン（アニメ後）に子を追従させるシステム。
 * @details TransformUpdateSystem の Solve と ModelPoseUpdateSystem の bake が
 *          完了した後に実行する。attach 先の worldMatrix を
 *          「自ローカル × (ボーン行列 × 親world)」で上書きし、サブツリーへ伝播する。
 */
class BoneAttachmentSystem : public CCL::ECS::IfSystem<BoneAttachmentSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<BoneAttachmentComponent>> {
public:
    BoneAttachmentSystem() : IfSystem("BoneAttachmentSystem") {}
    void Update(float dt) override;

private:
    // 1 つの attach 先を解決し、worldMatrix を確定させる。
    void ResolveAttachment(CCL::ECS::EntityID id, TransformComponent& trans, BoneAttachmentComponent& attach);

    // attach 先のローカル小階層へ world を再伝播する（対象は小規模前提）。
    void PropagateSubtree(CCL::ECS::EntityID firstChild, const DirectX::XMFLOAT4X4& parentWorld);

    // 当該エンティティが Model を持つなら world で bake し直す。
    void BakeModelIfPresent(CCL::ECS::EntityID id, const DirectX::XMFLOAT4X4& world);
};