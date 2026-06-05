#include "TransformUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Transform/Components/PendingParentComponent.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <vector>

using namespace CCL::ECS;
using namespace DirectX;

TransformUpdateSystem::TransformUpdateSystem() : IfSystem("TransformUpdateSystem") {}

static inline XMMATRIX ComposeLocal(const TransformComponent& t) {
    return XMMatrixAffineTransformation(
        XMLoadFloat3(&t.scale), XMVectorZero(),
        XMLoadFloat4(&t.rotation), XMLoadFloat3(&t.position));
}

void TransformUpdateSystem::Update(float dt)
{
    ProcessPendingParents();

    // 1. トポロジ変化検出＋ゴースト修復（compose しない＝軽量シリアル）
    if (ScanDetectTopology()) {
        RepairBrokenLinks();
        _cache.Rebuild(*_world);
    }

    // 2. ★ホットパス：ルートを並列インプレース計算（1万体の上下運動はここでスケール）
    ComputeRootsParallel();

    // 3. 非ルート（少数）を depth 順に解決。フラットシーンでは Count()==0 で即抜け。
    SolveHierarchy();
}

// =========================================================================
// 変化検出（compose なし）。重い処理を入れないことが性能の肝。
// =========================================================================
bool TransformUpdateSystem::ScanDetectTopology()
{
    bool   needRebuild = false;
    size_t matchedNonRoots = 0;

    ForEachWithID([&](EntityID id, TransformComponent& t) {
        EntityID parentId = t.parent;

        // ゴーストリンク修復（IsEntityValid は軽い。ここ1か所に集約）
        if (parentId != 0 && !_world->IsEntityValid(parentId)) {
            t.parent = 0; t.prevSiblingID = 0; t.nextSiblingID = 0; t.isDirty = true;
            parentId = 0; needRebuild = true;
        }

        const int oi = _cache.OrderOf(id);

        if (parentId == 0) {
            if (oi >= 0) needRebuild = true;   // 非ルート→ルートに変化
            return;                            // ルートはキャッシュ対象外
        }
        // 非ルート
        if (oi < 0) { needRebuild = true; return; }      // 新規の非ルート
        ++matchedNonRoots;
        if (_cache.ParentIdOf(oi) != parentId) needRebuild = true;  // 再親付け
        });

    if (matchedNonRoots != _cache.Count()) needRebuild = true;       // 非ルートの破棄
    return needRebuild;
}

// =========================================================================
// ルートの並列インプレース計算
// =========================================================================
void TransformUpdateSystem::ComputeRootsParallel()
{
    ForEachParallel([](TransformComponent& t) {
        if (t.parent != 0) return;                               // 子は SolveHierarchy で
        if (t.isStatic && !t.isDirty && !t.isTeleported) return; // 静止物のみスキップ
        XMStoreFloat4x4(&t.worldMatrix, ComposeLocal(t));
        t.isDirty = false;
        });
}

// =========================================================================
// 非ルートを depth 順に解決（compose + 親world継承 + 書き戻しを1ループで）
// =========================================================================
void TransformUpdateSystem::SolveHierarchy()
{
    const size_t n = _cache.Count();
    for (size_t i = 0; i < n; ++i) {
        const int idx = static_cast<int>(i);
        const EntityID id = _cache.EntityAt(idx);

        auto* t = _world->GetComponent<TransformComponent>(id);
        if (!t) continue;

        const XMMATRIX local = ComposeLocal(*t);

        // 親world：非ルート親 → dense（既に算出済み）／ルート親 → コンポーネント（並列パスで算出済み）
        const int p = _cache.ParentInOrderOf(idx);
        XMMATRIX parentW;
        if (p >= 0) {
            parentW = _cache.WorldAt(p);
        }
        else {
            auto* pt = _world->GetComponent<TransformComponent>(_cache.ParentIdOf(idx));
            parentW = pt ? XMLoadFloat4x4(&pt->worldMatrix) : XMMatrixIdentity();
        }

        const XMMATRIX world = XMMatrixMultiply(local, parentW);
        _cache.StoreWorld(idx, world);          // 後続の子が読むため dense にも残す
        XMStoreFloat4x4(&t->worldMatrix, world); // その場で書き戻し
        t->isDirty = false;
    }
}

// =========================================================================
// 保留親の適用（既存ロジック）
// =========================================================================
void TransformUpdateSystem::ProcessPendingParents()
{
    // 注意: View は全 Chunk 走査。0 件なら body は回らないが、頻発するなら
    //       PendingParentComponent 件数カウンタで早期 return 化するとさらに軽い。
    std::vector<EntityID> pending;
    for (auto id : _world->View<PendingParentComponent>()) {
        pending.push_back(id);
    }

    for (auto id : pending) {
        auto* p = _world->GetComponent<PendingParentComponent>(id);
        if (p) {
            if (p->parentID == 0 || p->parentID == InvalidEntityID) {
                if (_world->IsEntityValid(id)) {
                    DetachChild(*_world, id);
                    if (auto* trans = _world->GetComponent<TransformComponent>(id)) trans->isDirty = true;
                }
            }
            else if (_world->IsEntityValid(p->parentID) && _world->IsEntityValid(id)) {
                SetParent(*_world, id, p->parentID);
                if (auto* trans = _world->GetComponent<TransformComponent>(id)) trans->isDirty = true;
            }
        }
        _world->RequestRemoveComponent<PendingParentComponent>(id);
    }
}

// =========================================================================
// リンク切れ修復（既存ロジック・Rebuild 直前のみ実行）
// =========================================================================
void TransformUpdateSystem::RepairBrokenLinks()
{
    ForEachWithID([&](EntityID id, TransformComponent& trans) {
        if (trans.parent != 0) {
            auto* parentTrans = _world->GetComponent<TransformComponent>(trans.parent);
            if (parentTrans) {
                const bool isFirstChild = (parentTrans->firstChildID == id);
                const bool hasSibling = (trans.nextSiblingID != 0 || trans.prevSiblingID != 0);
                if (!isFirstChild && !hasSibling) {
                    SetParent(*_world, id, trans.parent);  // 正しい親子関係を結び直す
                }
            }
        }
        });
}

// =========================================================================
// ↓↓↓ 以下 static ヘルパーは既存のまま（変更なし） ↓↓↓
// =========================================================================
void TransformUpdateSystem::SetParent(Core::World& world, EntityID childID, EntityID newParentID)
{
    if (childID == newParentID) return;
    if (!world.IsEntityValid(childID)) return;

    DetachChild(world, childID);

    if (newParentID == 0) return;
    if (!world.IsEntityValid(newParentID)) return;

    auto* childTrans = world.GetComponent<TransformComponent>(childID);
    auto* parentTrans = world.GetComponent<TransformComponent>(newParentID);
    if (!childTrans || !parentTrans) return;

    childTrans->parent = newParentID;

    EntityID oldFirstChildID = parentTrans->firstChildID;
    childTrans->nextSiblingID = oldFirstChildID;
    childTrans->prevSiblingID = 0;

    if (oldFirstChildID != 0) {
        if (auto* oldFirstTrans = world.GetComponent<TransformComponent>(oldFirstChildID)) {
            oldFirstTrans->prevSiblingID = childID;
        }
    }
    parentTrans->firstChildID = childID;
}

void TransformUpdateSystem::DetachChild(Core::World& world, EntityID childID)
{
    auto* childTrans = world.GetComponent<TransformComponent>(childID);
    if (!childTrans || childTrans->parent == 0) return;

    EntityID parentID = childTrans->parent;
    auto* parentTrans = world.GetComponent<TransformComponent>(parentID);

    if (parentTrans) {
        if (parentTrans->firstChildID == childID) {
            parentTrans->firstChildID = childTrans->nextSiblingID;
        }
    }
    if (childTrans->prevSiblingID != 0) {
        if (auto* prevTrans = world.GetComponent<TransformComponent>(childTrans->prevSiblingID))
            prevTrans->nextSiblingID = childTrans->nextSiblingID;
    }
    if (childTrans->nextSiblingID != 0) {
        if (auto* nextTrans = world.GetComponent<TransformComponent>(childTrans->nextSiblingID))
            nextTrans->prevSiblingID = childTrans->prevSiblingID;
    }
    childTrans->parent = 0;
    childTrans->prevSiblingID = 0;
    childTrans->nextSiblingID = 0;
}

void TransformUpdateSystem::DestroyEntityWithHierarchy(Core::World& world, EntityID entity, bool destroyChildren)
{
    if (!world.IsEntityValid(entity)) return;
    auto* trans = world.GetComponent<TransformComponent>(entity);
    if (!trans) { world.RequestDestroyEntity(entity); return; }

    if (destroyChildren) {
        EntityID childID = trans->firstChildID;
        while (childID != 0) {
            auto* childTrans = world.GetComponent<TransformComponent>(childID);
            if (!childTrans) break;
            EntityID nextChild = childTrans->nextSiblingID;
            DestroyEntityWithHierarchy(world, childID, true);
            childID = nextChild;
        }
    }
    else {
        EntityID childID = trans->firstChildID;
        while (childID != 0) {
            auto* childTrans = world.GetComponent<TransformComponent>(childID);
            if (!childTrans) break;
            EntityID nextChild = childTrans->nextSiblingID;
            DetachChild(world, childID);
            childID = nextChild;
        }
    }
    DetachChild(world, entity);
    world.RequestDestroyEntity(entity);
}

void TransformUpdateSystem::MarkForDestruction(Core::World& world, EntityID entity, bool destroyChildren)
{
    if (!world.IsEntityValid(entity)) return;
    if (world.HasComponent<Tag::DestroyTag>(entity)) return;

    world.AddComponent<Tag::DestroyTag>(entity);

    auto* trans = world.GetComponent<TransformComponent>(entity);
    if (!trans) return;

    if (destroyChildren) {
        EntityID childID = trans->firstChildID;
        while (childID != 0) {
            auto* childTrans = world.GetComponent<TransformComponent>(childID);
            if (!childTrans) break;
            EntityID nextChild = childTrans->nextSiblingID;
            MarkForDestruction(world, childID, true);
            childID = nextChild;
        }
    }
}

REGISTER_LOGIC_SYSTEM(TransformUpdateSystem, Priority::LogicStage::L02_PostUpdate);