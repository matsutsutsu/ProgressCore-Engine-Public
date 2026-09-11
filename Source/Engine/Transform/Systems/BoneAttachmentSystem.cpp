#include "BoneAttachmentSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/Model.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <vector>
#include <utility>

using namespace CCL::ECS;
using namespace DirectX;

void BoneAttachmentSystem::Update(float dt)
{
    // attach 先のみに絞られているので、毎フレーム全件処理して常に最新ボーンへ追従する。
    ForEachWithID([&](EntityID id, TransformComponent& trans, BoneAttachmentComponent& attach) {
        ResolveAttachment(id, trans, attach);
        });
}

void BoneAttachmentSystem::ResolveAttachment(EntityID id, TransformComponent& trans, BoneAttachmentComponent& attach)
{
    const EntityID parentId = trans.parent;
    if (parentId == 0) return;

    auto* parentTrans = m_world->GetComponent<TransformComponent>(parentId);
    auto* parentModel = m_world->GetComponent<ModelComponent>(parentId);
    if (!parentTrans || !parentModel) return;

    Model* model = parentModel->GetModel();
    if (!model) return;

    // ボーンインデックスを初回のみ解決してキャッシュ
    if (attach.cachedBoneIndex == -1 && !attach.boneName.empty()) {
        attach.cachedBoneIndex = model->GetNodeIndex(attach.boneName.c_str());
    }
    if (attach.cachedBoneIndex == -1) return;  // 見つからなければ追従しない

    const auto& nodes = model->GetNodes();
    if (attach.cachedBoneIndex >= static_cast<int>(nodes.size())) return;  // 安全策

    // boneWorld = ボーンのモデルローカル行列 × 親のworld
    const XMMATRIX boneLocal = XMLoadFloat4x4(&nodes[attach.cachedBoneIndex].globalTransform);
    const XMMATRIX parentWorld = XMLoadFloat4x4(&parentTrans->worldMatrix);
    const XMMATRIX boneWorld = XMMatrixMultiply(boneLocal, parentWorld);

    // 自ローカル（オフセット）を掛けて world を確定
    const XMMATRIX world = XMMatrixMultiply(trans.ComposeLocalMatrix(), boneWorld);

    XMFLOAT4X4 world4;
    XMStoreFloat4x4(&world4, world);
    trans.worldMatrix = world4;          // 仮 world（Transform 側）を上書き

    // モデルのノード行列(bake)はここでは焼かない。BoneAttachmentRenderSystem(R04)の仕事。
    //   焼いた行列は描画にしか使われないのに、本システムは FixedUpdate 側で
    //   1描画フレームに最大4回まわる（Framework::MAX_STEPS）。しかも Render の
    //   ModelPoseUpdateSystem(R03) が全モデルを丸ごと焼き直すため、ここでの bake は
    //   一度も読まれずに上書きされて消えていた。
    PropagateSubtree(trans.firstChildID, world4);  // 小階層へ再伝播
}

void BoneAttachmentSystem::PropagateSubtree(EntityID firstChild, const XMFLOAT4X4& parentWorld)
{
    // attach 先のサブツリー（武器→銃口点 等）は小規模前提。明示スタックで pre-order 伝播。
    std::vector<std::pair<EntityID, XMFLOAT4X4>> stack;

    EntityID c = firstChild;
    while (c != 0) {
        auto* ct = m_world->GetComponent<TransformComponent>(c);
        if (!ct) break;
        const EntityID next = ct->nextSiblingID;
        stack.emplace_back(c, parentWorld);
        c = next;
    }

    while (!stack.empty()) {
        const auto [cid, pw] = stack.back();
        stack.pop_back();

        auto* t = m_world->GetComponent<TransformComponent>(cid);
        if (!t) continue;

        const XMMATRIX world = XMMatrixMultiply(t->ComposeLocalMatrix(), XMLoadFloat4x4(&pw));
        XMFLOAT4X4 world4;
        XMStoreFloat4x4(&world4, world);
        t->worldMatrix = world4;

        EntityID gc = t->firstChildID;
        while (gc != 0) {
            auto* gct = m_world->GetComponent<TransformComponent>(gc);
            if (!gct) break;
            const EntityID next = gct->nextSiblingID;
            stack.emplace_back(gc, world4);
            gc = next;
        }
    }
}

// 当たり判定用の worldMatrix を確定させるロジック側の追従。
//    読んでいる親のボーン行列(globalTransform)は Render の ModelPoseUpdateSystem(R03)が
//    焼くので、ここで見えるのは前フレームの描画時点の姿勢になる（1フレーム遅れ）。
//    見た目は BoneAttachmentRenderSystem(R04)が同フレームで別途解決している。
REGISTER_LOGIC_SYSTEM(BoneAttachmentSystem, Priority::LogicStage::L03_PrePhysics);