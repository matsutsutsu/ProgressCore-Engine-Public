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
    ForEachWithID([&](EntityID id, TransformComponent &trans, const BoneAttachmentComponent &attach) {
        ResolveRenderAttachment(id, trans, attach);
    });
}

void BoneAttachmentRenderSystem::ResolveRenderAttachment(EntityID id, TransformComponent &trans, const BoneAttachmentComponent &attach)
{
    const EntityID parentId = trans.parent;
    if (parentId == 0) return;

    auto *parentTrans = m_world->GetComponent<TransformComponent>(parentId);
    auto *parentModel = m_world->GetComponent<ModelComponent>(parentId);
    if (!parentTrans || !parentModel) return;

    Model *model = parentModel->GetModel();
    if (!model) return;

    // boneName の解決結果（cachedBoneIndex）は、ロジック版が既に埋めている前提。
    // 未解決（-1）ならロジック版がまだ走っていない初回などなので、描画は1フレームだけ待つ
    if (attach.cachedBoneIndex == -1) return;

    const auto &nodes = model->GetNodes();
    if (attach.cachedBoneIndex >= static_cast<int>(nodes.size())) return;

    // globalTransform はルート非依存なので、ロジック版と同じ値。違うのは「親の補間済みワールド」を掛ける点だけ
    const XMMATRIX boneGlobal   = XMLoadFloat4x4(&nodes[attach.cachedBoneIndex].globalTransform);
    const XMMATRIX parentRWorld = XMLoadFloat4x4(&parentTrans->renderWorldMatrix); // 補間済み
    const XMMATRIX boneRWorld   = XMMatrixMultiply(boneGlobal, parentRWorld);

    const XMMATRIX world = XMMatrixMultiply(trans.ComposeLocalMatrix(), boneRWorld);

    XMFLOAT4X4 world4;
    XMStoreFloat4x4(&world4, world);
    trans.renderWorldMatrix = world4; // 描画用の行列にだけ書く（worldMatrix は触らない）

    BakeModelIfPresent(id, world4); // 自分が Model を持つなら、確定した world で焼き直す
    PropagateRenderSubtree(trans.firstChildID, world4);
}

// ModelPoseUpdateSystem（R03）は追従先も「前フレームの renderWorldMatrix」で
// 先に焼いてしまう。このシステム（R04）で正しい renderWorldMatrix が確定した後に
// 焼き直し、後から書いた方を採用させることで同じフレームの中で整合を取る。
//   ※ これをやらないと、武器などの追従物だけ描画が1フレーム遅れる。
void BoneAttachmentRenderSystem::BakeModelIfPresent(EntityID id, const XMFLOAT4X4 &world)
{
    if (auto *mc = m_world->GetComponent<ModelComponent>(id))
    {
        if (Model *m = mc->GetModel())
        {
            m->UpdateTransform(world);
        }
    }
}

void BoneAttachmentRenderSystem::PropagateRenderSubtree(EntityID firstChild, const XMFLOAT4X4 &parentRenderWorld)
{
    std::vector<std::pair<EntityID, XMFLOAT4X4>> stack;

    EntityID c = firstChild;
    while (c != 0)
    {
        auto *ct = m_world->GetComponent<TransformComponent>(c);
        if (!ct) break;
        const EntityID next = ct->nextSiblingID;
        stack.emplace_back(c, parentRenderWorld);
        c = next;
    }

    while (!stack.empty())
    {
        const auto [cid, pw] = stack.back();
        stack.pop_back();

        auto *t = m_world->GetComponent<TransformComponent>(cid);
        if (!t) continue;

        const XMMATRIX world = XMMatrixMultiply(t->ComposeLocalMatrix(), XMLoadFloat4x4(&pw));
        XMFLOAT4X4     world4;
        XMStoreFloat4x4(&world4, world);
        t->renderWorldMatrix = world4; // 描画用の行列だけ

        BakeModelIfPresent(cid, world4);

        EntityID gc = t->firstChildID;
        while (gc != 0)
        {
            auto *gct = m_world->GetComponent<TransformComponent>(gc);
            if (!gct) break;
            const EntityID next = gct->nextSiblingID;
            stack.emplace_back(gc, world4);
            gc = next;
        }
    }
}

// R00_Interpolation（親の renderWorldMatrix 確定）の後、R08_Main（描画）の前に置く。
// 親のボーン姿勢（globalTransform）は ModelPoseUpdateSystem（R03）が焼くので、その後の R04 帯にする
REGISTER_RENDER_SYSTEM(BoneAttachmentRenderSystem, Priority::RenderStage::R04_BoundsUpdate);