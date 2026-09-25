#include "TransformUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Transform/Components/PendingParentComponent.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <vector>

using namespace CCL::ECS;
using namespace DirectX;

TransformUpdateSystem::TransformUpdateSystem() : IfSystem("TransformUpdateSystem") {}

// ローカルの TRS から行列を作る（S * R * T）
static inline XMMATRIX ComposeLocal(const TransformComponent &t)
{
    return XMMatrixAffineTransformation(
        XMLoadFloat3(&t.scale), XMVectorZero(),
        XMLoadFloat4(&t.rotation), XMLoadFloat3(&t.position));
}

void TransformUpdateSystem::Initialize()
{
    // Render ステージの補間システムが同じ並び順を参照できるよう公開する
    if (!m_world->HasResource<TransformHierarchyCache *>())
    {
        m_world->AddResource<TransformHierarchyCache *>(&m_cache);
    }
}

void TransformUpdateSystem::Update(float dt)
{
    ProcessPendingParents();

    // 1. 親子関係の変化検出とゴーストリンクの修復（行列は計算しないので、直列でも軽い）
    if (ScanDetectTopology())
    {
        RepairBrokenLinks();
        m_cache.Rebuild(*m_world);
    }

    // 2. ルートを並列にその場で計算する。Entity 数が多いのはここなので、並列化の効果が大きい
    ComputeRootsParallel();

    // 3. 非ルート（少数）を親が先の順に解決する。親子の無いシーンでは Count() == 0 ですぐ抜ける
    SolveHierarchy();
}

// 毎ステップ全 Transform を回るので、行列計算などの重い処理は入れない
bool TransformUpdateSystem::ScanDetectTopology()
{
    bool   needRebuild     = false;
    size_t matchedNonRoots = 0;

    ForEachWithID([&](EntityID id, TransformComponent &t) {
        EntityID parentId = t.parent;

        // 破棄済みの親を指したままのリンクを外す（IsEntityValid は軽い。ここ1か所に集約）
        if (parentId != 0 && !m_world->IsEntityValid(parentId))
        {
            t.parent        = 0;
            t.prevSiblingID = 0;
            t.nextSiblingID = 0;
            t.isDirty       = true;
            parentId        = 0;
            needRebuild     = true;
        }

        const int oi = m_cache.OrderOf(id);

        if (parentId == 0)
        {
            if (oi >= 0) needRebuild = true; // 非ルートからルートに変わった
            return;                          // ルートはキャッシュの対象外
        }

        if (oi < 0)
        {
            // キャッシュに無い非ルート＝新しく親子付けされた
            needRebuild = true;
            return;
        }
        ++matchedNonRoots;
        if (m_cache.ParentIdOf(oi) != parentId) needRebuild = true; // 別の親に付け替えられた
    });

    if (matchedNonRoots != m_cache.Count()) needRebuild = true; // キャッシュ上の非ルートが破棄された
    return needRebuild;
}

void TransformUpdateSystem::ComputeRootsParallel()
{
    ForEachParallel([](TransformComponent &t) {
        if (t.parent != 0) return;                               // 子は SolveHierarchy で計算する
        if (t.isStatic && !t.isDirty && !t.isTeleported) return; // 動いていない静的物体だけ省く
        XMStoreFloat4x4(&t.worldMatrix, ComposeLocal(t));
        t.isDirty = false;
    });
}

// ローカル行列の合成・親のワールド行列の継承・書き戻しを1つのループで行う
void TransformUpdateSystem::SolveHierarchy()
{
    const size_t n = m_cache.Count();
    for (size_t i = 0; i < n; ++i)
    {
        const int      idx = static_cast<int>(i);
        const EntityID id  = m_cache.EntityAt(idx);

        auto *t = m_world->GetComponent<TransformComponent>(id);
        if (!t) continue;

        const XMMATRIX local = ComposeLocal(*t);

        // 親のワールド行列: 親が非ルートならキャッシュの作業領域（算出済み）、
        //                   ルートならコンポーネント（並列パスで算出済み）から読む
        const int p = m_cache.ParentInOrderOf(idx);
        XMMATRIX  parentW;
        if (p >= 0)
        {
            parentW = m_cache.WorldAt(p);
        }
        else
        {
            auto *pt = m_world->GetComponent<TransformComponent>(m_cache.ParentIdOf(idx));
            parentW  = pt ? XMLoadFloat4x4(&pt->worldMatrix) : XMMatrixIdentity();
        }

        const XMMATRIX world = XMMatrixMultiply(local, parentW);
        m_cache.StoreWorld(idx, world);          // 後続の子が読むため作業領域にも残す
        XMStoreFloat4x4(&t->worldMatrix, world); // その場で書き戻し
        t->isDirty = false;
    }
}

void TransformUpdateSystem::ProcessPendingParents()
{
    // View は全 Chunk を走査する。0 件なら中身は回らないが、重くなるようなら
    // PendingParentComponent の件数を数えておき、0 なら先に抜けるようにする
    std::vector<EntityID> pending;
    for (auto id : m_world->View<PendingParentComponent>())
    {
        pending.push_back(id);
    }

    for (auto id : pending)
    {
        auto *p = m_world->GetComponent<PendingParentComponent>(id);
        if (p)
        {
            // 親が 0 / 無効なら、親から切り離す予約
            if (p->parentID == 0 || p->parentID == InvalidEntityID)
            {
                if (m_world->IsEntityValid(id))
                {
                    DetachChild(*m_world, id);
                    if (auto *trans = m_world->GetComponent<TransformComponent>(id)) trans->isDirty = true;
                }
            }
            else if (m_world->IsEntityValid(p->parentID) && m_world->IsEntityValid(id))
            {
                SetParent(*m_world, id, p->parentID);
                if (auto *trans = m_world->GetComponent<TransformComponent>(id)) trans->isDirty = true;
            }
        }
        // 親が無効で適用できなかった予約も、残し続けないよう必ず外す
        m_world->RequestRemoveComponent<PendingParentComponent>(id);
    }
}

// キャッシュを作り直す直前だけ実行する
void TransformUpdateSystem::RepairBrokenLinks()
{
    ForEachWithID([&](EntityID id, TransformComponent &trans) {
        if (trans.parent != 0)
        {
            auto *parentTrans = m_world->GetComponent<TransformComponent>(trans.parent);
            if (parentTrans)
            {
                const bool isFirstChild = (parentTrans->firstChildID == id);
                const bool hasSibling   = (trans.nextSiblingID != 0 || trans.prevSiblingID != 0);
                if (!isFirstChild && !hasSibling)
                {
                    SetParent(*m_world, id, trans.parent); // 兄弟リストに繋ぎ直す
                }
            }
        }
    });
}

// ============================================================
// 親子関係の操作
// ============================================================

// 子は親の子リストの先頭に挿入する
void TransformUpdateSystem::SetParent(World &world, EntityID childID, EntityID newParentID)
{
    if (childID == newParentID) return;
    if (!world.IsEntityValid(childID)) return;

    DetachChild(world, childID);

    if (newParentID == 0) return;
    if (!world.IsEntityValid(newParentID)) return;

    auto *childTrans  = world.GetComponent<TransformComponent>(childID);
    auto *parentTrans = world.GetComponent<TransformComponent>(newParentID);
    if (!childTrans || !parentTrans) return;

    childTrans->parent = newParentID;

    EntityID oldFirstChildID  = parentTrans->firstChildID;
    childTrans->nextSiblingID = oldFirstChildID;
    childTrans->prevSiblingID = 0;

    if (oldFirstChildID != 0)
    {
        if (auto *oldFirstTrans = world.GetComponent<TransformComponent>(oldFirstChildID))
        {
            oldFirstTrans->prevSiblingID = childID;
        }
    }
    parentTrans->firstChildID = childID;
}

void TransformUpdateSystem::DetachChild(World &world, EntityID childID)
{
    auto *childTrans = world.GetComponent<TransformComponent>(childID);
    if (!childTrans || childTrans->parent == 0) return;

    EntityID parentID    = childTrans->parent;
    auto    *parentTrans = world.GetComponent<TransformComponent>(parentID);

    if (parentTrans)
    {
        if (parentTrans->firstChildID == childID)
        {
            parentTrans->firstChildID = childTrans->nextSiblingID;
        }
    }
    if (childTrans->prevSiblingID != 0)
    {
        if (auto *prevTrans = world.GetComponent<TransformComponent>(childTrans->prevSiblingID))
            prevTrans->nextSiblingID = childTrans->nextSiblingID;
    }
    if (childTrans->nextSiblingID != 0)
    {
        if (auto *nextTrans = world.GetComponent<TransformComponent>(childTrans->nextSiblingID))
            nextTrans->prevSiblingID = childTrans->prevSiblingID;
    }
    childTrans->parent        = 0;
    childTrans->prevSiblingID = 0;
    childTrans->nextSiblingID = 0;
}

void TransformUpdateSystem::DestroyEntityWithHierarchy(World &world, EntityID entity, bool destroyChildren)
{
    if (!world.IsEntityValid(entity)) return;
    auto *trans = world.GetComponent<TransformComponent>(entity);
    if (!trans)
    {
        world.RequestDestroyEntity(entity);
        return;
    }

    if (destroyChildren)
    {
        EntityID childID = trans->firstChildID;
        while (childID != 0)
        {
            auto *childTrans = world.GetComponent<TransformComponent>(childID);
            if (!childTrans) break;
            EntityID nextChild = childTrans->nextSiblingID;
            DestroyEntityWithHierarchy(world, childID, true);
            childID = nextChild;
        }
    }
    else
    {
        EntityID childID = trans->firstChildID;
        while (childID != 0)
        {
            auto *childTrans = world.GetComponent<TransformComponent>(childID);
            if (!childTrans) break;
            EntityID nextChild = childTrans->nextSiblingID;
            DetachChild(world, childID);
            childID = nextChild;
        }
    }
    DetachChild(world, entity);
    world.RequestDestroyEntity(entity);
}

// ============================================================
// 物理との受け渡し（ワールド値 → ローカル値）
// ============================================================
void TransformUpdateSystem::WriteWorldTransform(World &world, EntityID entity,
                                                TransformComponent &trans, const DirectX::XMFLOAT3 &worldPos,
                                                const DirectX::XMFLOAT4 &worldRot)
{
    using namespace DirectX;

    const EntityID parentID = trans.parent.id;

    // 親がいなければローカル＝ワールド。大多数はここで終わる（追加コストなし）。
    if (parentID == 0)
    {
        trans.position = worldPos;
        trans.rotation = worldRot;
        trans.isStatic = false;
        return;
    }

    const auto *parentTrans = world.GetComponent<TransformComponent>(parentID);
    if (!parentTrans)
    {
        // 親リンクが切れている（破棄直後など）。ワールド値のまま置くのが最も無難。
        trans.position = worldPos;
        trans.rotation = worldRot;
        trans.isStatic = false;
        return;
    }

    const XMMATRIX parentWorld = XMLoadFloat4x4(&parentTrans->worldMatrix);

    // 位置: 親のワールド行列の逆行列を通す
    XMVECTOR localPos = XMVector3Transform(
        XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f),
        XMMatrixInverse(nullptr, parentWorld));
    XMStoreFloat3(&trans.position, localPos);

    // 回転: 親のワールド回転の逆をかける
    XMVECTOR pScale, pRot, pTrans;
    if (XMMatrixDecompose(&pScale, &pRot, &pTrans, parentWorld))
    {
        XMVECTOR localRot = XMQuaternionMultiply(
            XMLoadFloat4(&worldRot), XMQuaternionInverse(pRot));
        XMStoreFloat4(&trans.rotation, XMQuaternionNormalize(localRot));
    }
    else
    {
        // 分解できない行列（スケール0など）。回転は触らず位置だけ反映しておく。
        trans.rotation = worldRot;
    }

    trans.isStatic = false;
}

void TransformUpdateSystem::WriteWorldPosition(World &world, EntityID entity,
                                               TransformComponent &trans, const DirectX::XMFLOAT3 &worldPos)
{
    // 親がいる場合、WriteWorldTransform は rotation も書き換えてしまうので、
    // 回転を据え置くためにここでは位置だけを自前で処理する
    using namespace DirectX;

    const EntityID parentID = trans.parent.id;
    if (parentID == 0)
    {
        trans.position = worldPos;
        trans.isStatic = false;
        return;
    }

    const auto *parentTrans = world.GetComponent<TransformComponent>(parentID);
    if (!parentTrans)
    {
        trans.position = worldPos;
        trans.isStatic = false;
        return;
    }

    const XMMATRIX parentWorld = XMLoadFloat4x4(&parentTrans->worldMatrix);
    XMVECTOR       localPos    = XMVector3Transform(
        XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f),
        XMMatrixInverse(nullptr, parentWorld));
    XMStoreFloat3(&trans.position, localPos);
    trans.isStatic = false;
}

void TransformUpdateSystem::MarkForDestruction(World &world, EntityID entity, bool destroyChildren)
{
    if (!world.IsEntityValid(entity)) return;
    if (world.HasComponent<Tag::DestroyTag>(entity)) return;

    world.AddComponent<Tag::DestroyTag>(entity);

    auto *trans = world.GetComponent<TransformComponent>(entity);
    if (!trans) return;

    if (destroyChildren)
    {
        EntityID childID = trans->firstChildID;
        while (childID != 0)
        {
            auto *childTrans = world.GetComponent<TransformComponent>(childID);
            if (!childTrans) break;
            EntityID nextChild = childTrans->nextSiblingID;
            MarkForDestruction(world, childID, true);
            childID = nextChild;
        }
    }
}

REGISTER_LOGIC_SYSTEM(TransformUpdateSystem, Priority::LogicStage::L02_PostUpdate);