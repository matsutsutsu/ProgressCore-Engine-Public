#pragma once
// ===========================================================================
// 概要: 親 Entity のボーンに子を追従させる System（ロジック側・当たり判定用）。
//
// 責務:
//   - 追従先の worldMatrix を「自分のローカル × (ボーン行列 × 親のワールド)」で上書きする
//   - 追従先の子孫へ worldMatrix を伝播する
//   - ボーン名からボーン番号への解決（初回のみ）
//
// 備考: TransformUpdateSystem の解決（L02_PostUpdate）の後、物理の前（L03_PrePhysics）で走る。
//       読むボーン行列は描画側（ModelPoseUpdateSystem, R03）が焼いたものなので、
//       前フレームの描画時点の姿勢になる（1フレーム遅れ）。
//       見た目は BoneAttachmentRenderSystem が同じフレームの中で別途解決する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Transform/Components/BoneAttachmentComponent.h"
#include <DirectXMath.h>

class BoneAttachmentSystem : public CCL::ECS::IfSystem<BoneAttachmentSystem,
                                                       CCL::ECS::Write<TransformComponent>,
                                                       CCL::ECS::Write<BoneAttachmentComponent>>
{
public:
    BoneAttachmentSystem() : IfSystem("BoneAttachmentSystem") {}
    void Update(float dt) override;

private:
    // 追従先1つを解決し、worldMatrix を確定させる
    void ResolveAttachment(CCL::ECS::EntityID id, TransformComponent &trans, BoneAttachmentComponent &attach);

    // 追従先の子孫へワールド行列を伝播する（子孫は少数の前提）
    void PropagateSubtree(CCL::ECS::EntityID firstChild, const DirectX::XMFLOAT4X4 &parentWorld);
};