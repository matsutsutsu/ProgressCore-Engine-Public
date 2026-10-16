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
    // 対象は BoneAttachmentComponent を持つ Entity だけなので、毎ステップ全件を最新のボーンへ追従させる
    ForEachWithID([&](EntityID id, TransformComponent &trans, BoneAttachmentComponent &attach) {
        ResolveAttachment(id, trans, attach);
    });
}

void BoneAttachmentSystem::ResolveAttachment(EntityID id, TransformComponent &trans, BoneAttachmentComponent &attach)
{
    // 処理フロー:
    // 1. 親の Transform とモデルを取る
    // 2. ボーン番号を解決する（初回だけ）
    // 3. 「自分のローカル × ボーン行列 × 親のワールド」で worldMatrix を確定する
    // 4. 子孫へ伝える

    const EntityID parentId = trans.parent;
    if (parentId == 0) return;

    auto *parentTrans = m_world->GetComponent<TransformComponent>(parentId);
    auto *parentModel = m_world->GetComponent<ModelComponent>(parentId);
    if (!parentTrans || !parentModel) return;

    Model *model = parentModel->GetModel();
    if (!model) return;

    // ボーン番号は初回だけ名前から解決してキャッシュする
    if (attach.cachedBoneIndex == -1 && !attach.boneName.empty())
    {
        attach.cachedBoneIndex = model->GetNodeIndex(attach.boneName.c_str());
    }
    if (attach.cachedBoneIndex == -1) return; // 見つからなければ追従しない

    const auto &nodes = model->GetNodes();
    if (attach.cachedBoneIndex >= static_cast<int>(nodes.size())) return; // モデルが差し替わった場合など

    // ボーンのワールド行列 = ボーンのモデル空間での行列 × 親のワールド行列
    const XMMATRIX boneLocal   = XMLoadFloat4x4(&nodes[attach.cachedBoneIndex].globalTransform);
    const XMMATRIX parentWorld = XMLoadFloat4x4(&parentTrans->worldMatrix);
    const XMMATRIX boneWorld   = XMMatrixMultiply(boneLocal, parentWorld);

    // 自分のローカル（ボーンからのオフセット）を掛けて確定する
    const XMMATRIX world = XMMatrixMultiply(trans.ComposeLocalMatrix(), boneWorld);

    XMFLOAT4X4 world4;
    XMStoreFloat4x4(&world4, world);
    trans.worldMatrix = world4; // TransformUpdateSystem が親基準で計算した値を上書きする

    // モデルのノード行列はここでは焼かない（BoneAttachmentRenderSystem の仕事）。
    //   焼いた行列は描画にしか使われないのに、このシステムは FixedUpdate 側で
    //   1描画フレームに最大4回まわる（Framework::Run の MAX_STEPS）。しかも
    //   ModelPoseUpdateSystem（R03）が全モデルを丸ごと焼き直すため、ここで焼いても
    //   一度も読まれずに上書きされる。
    PropagateSubtree(trans.firstChildID, world4);
}

void BoneAttachmentSystem::PropagateSubtree(EntityID firstChild, const XMFLOAT4X4 &parentWorld)
{
    // 追従先の子孫（武器→銃口の位置 など）は少数の前提。再帰ではなく明示スタックで親→子の順に伝播する
    std::vector<std::pair<EntityID, XMFLOAT4X4>> stack;

    EntityID c = firstChild;
    while (c != 0)
    {
        auto *ct = m_world->GetComponent<TransformComponent>(c);
        if (!ct) break;
        const EntityID next = ct->nextSiblingID;
        stack.emplace_back(c, parentWorld);
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
        t->worldMatrix = world4;

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

// JoltPushSystem（L03_PushToPhysics）が剣などの剛体を追従位置へ押し込む前に確定させる
REGISTER_LOGIC_SYSTEM(BoneAttachmentSystem, Priority::LogicStage::L03_PrePhysics);