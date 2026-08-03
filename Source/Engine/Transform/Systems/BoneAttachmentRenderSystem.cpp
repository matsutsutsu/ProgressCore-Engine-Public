// BoneAttachmentRenderSystem.cpp
#include "BoneAttachmentRenderSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <vector>
#include <utility>

using namespace CCL::ECS;
using namespace DirectX;

void BoneAttachmentRenderSystem::Update(float dt)
{
    ForEachWithID([&](EntityID id, TransformComponent& trans, const BoneAttachmentComponent& attach) {
        ResolveRenderAttachment(id, trans, attach);
    });
}

void BoneAttachmentRenderSystem::ResolveRenderAttachment(EntityID id, TransformComponent& trans, const BoneAttachmentComponent& attach)
{
    const EntityID parentId = trans.parent;
    if (parentId == 0) return;

    auto* parentTrans = m_world->GetComponent<TransformComponent>(parentId);
    auto* parentModel = m_world->GetComponent<ModelComponent>(parentId);
    if (!parentTrans || !parentModel) return;

    Model* model = parentModel->GetModel();
    if (!model) return;

    // boneName の解決結果(cachedBoneIndex)は Logic版が既に埋めている前提。
    //    未解決(-1)なら Logic版がまだ走っていない初回等なので、描画は1フレームだけスキップして待つ。
    if (attach.cachedBoneIndex == -1) return;

    const auto& nodes = model->GetNodes();
    if (attach.cachedBoneIndex >= static_cast<int>(nodes.size())) return;

    // globalTransform はルート非依存＝Logic版と同一の値。ここでは「親の補間済みworld」を掛ける点だけが違う。
    const XMMATRIX boneGlobal  = XMLoadFloat4x4(&nodes[attach.cachedBoneIndex].globalTransform);
    const XMMATRIX parentRWorld = XMLoadFloat4x4(&parentTrans->renderWorldMatrix); // 補間済み
    const XMMATRIX boneRWorld  = XMMatrixMultiply(boneGlobal, parentRWorld);

    const XMMATRIX world = XMMatrixMultiply(trans.ComposeLocalMatrix(), boneRWorld);

    XMFLOAT4X4 world4;
    XMStoreFloat4x4(&world4, world);
    trans.renderWorldMatrix = world4; // 描画専用行列にのみ書き込む（worldMatrixは触らない）

    PropagateRenderSubtree(trans.firstChildID, world4);
}

void BoneAttachmentRenderSystem::PropagateRenderSubtree(EntityID firstChild, const XMFLOAT4X4& parentRenderWorld)
{
    std::vector<std::pair<EntityID, XMFLOAT4X4>> stack;

    EntityID c = firstChild;
    while (c != 0) {
        auto* ct = m_world->GetComponent<TransformComponent>(c);
        if (!ct) break;
        const EntityID next = ct->nextSiblingID;
        stack.emplace_back(c, parentRenderWorld);
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
        t->renderWorldMatrix = world4; // 描画専用のみ

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

// R00_Interpolation(親のrenderWorldMatrix確定)の後、R08_Main(描画)の前。
//    親のボーン姿勢(globalTransform)は Render版ModelPoseUpdateSystem(R03)が
//    renderWorldMatrix基準で焼くので、その後段のR04帯に置くのが安全。
REGISTER_RENDER_SYSTEM(BoneAttachmentRenderSystem, Priority::RenderStage::R04_BoundsUpdate);