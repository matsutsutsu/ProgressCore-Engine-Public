// ===========================================================================
// HierarchyCleanupSystem.cpp
// --------------------------
// 役割:
//   DestroyTag を起点に、死亡Entityとその子階層を安全に回収するL07システム。
//   通常Entityは破棄、プール対象(PooledParticleComponent)の階層はプールへ返す。
//
// 実装の意図(Why):
//   - 反復中の構造変更は危険なため、まず「誰を消す/返すか」を executionList に確定
//     させてから一括処理する。
//   - プール対象の emitterRoot は【階層を保ったまま】プールへ返す必要がある。よって
//     (a)子収集でプール対象には潜らない(配下はrootと一緒に返るため列挙しない)、
//     (b)プール対象はフラット化(子のparent=0化)しない、
//     (c)プール対象は破棄せず Prefab::ReturnParticleToPool で返却する。
//   - これを怠るとプール対象の子エミッタが個別破棄され、空のemitterRootがプールに
//     戻る(=壊れる)。複数エミッタ構成を維持するための必須処理。
// ===========================================================================
#include "HierarchyCleanupSystem.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <algorithm>
#include "Engine/Serialization/Factory/Prefab.h"
#include "Game/Utils/PooledParticleComponent.h"
#include <vector>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace CCL::ECS;

HierarchyCleanupSystem::HierarchyCleanupSystem() : IfSystem("HierarchyCleanupSystem") {}

void HierarchyCleanupSystem::Update(float dt)
{
    ExecuteCleanup(*_world);
}

// =========================================================================
// 実際のクリーンアップ処理 (static)
// =========================================================================
void HierarchyCleanupSystem::ExecuteCleanup(CCL::ECS::World& world)
{
    std::vector<EntityID> executionList;
    executionList.reserve(64);

    // 1. DestroyTag を持つ「削除起点」を見つけ、子も収集する(チャンク手動走査)。
    auto& chunks = world.GetChunkManager().GetChunks();
    for (auto& chunkPtr : chunks) {
        if (!chunkPtr) continue;
        auto* chunk = chunkPtr.get();
        if (!chunk->HasComponent<Tag::DestroyTag>()) continue;

        size_t count = chunk->GetEntityCount();
        const EntityID* entityIDs = chunk->GetEntityIDs();

        for (size_t i = 0; i < count; ++i) {
            if (chunk->IsEntityDestroyed(i)) continue;
            EntityID id = entityIDs[i];
            executionList.push_back(id);
            if (world.HasComponent<TransformComponent>(id)) {
                CollectChildrenRecursive(world, id, executionList);
            }
        }
    }

    // 2. 重複排除。
    std::sort(executionList.begin(), executionList.end());
    executionList.erase(std::unique(executionList.begin(), executionList.end()), executionList.end());

    // 3. 削除/返却の実行。
    for (EntityID targetID : executionList) {

        // --- プール対象(emitterRoot)は階層を保ったままプールへ返す ---
        // フラット化も破棄もしない。ReturnParticleToPool が親切り離し・休眠・返却を行う。
        if (world.HasComponent<PooledParticleComponent>(targetID)) {
            Prefab::ReturnParticleToPool(world, targetID);
            // 万一 DestroyTag が付いていても破棄させない(返却を優先)。
            if (world.HasComponent<Tag::DestroyTag>(targetID))
                world.RequestRemoveComponent<Tag::DestroyTag>(targetID);
            continue;
        }

        // --- 通常Entity ---
        if (world.HasComponent<TransformComponent>(targetID)) {
            auto* trans = world.GetComponent<TransformComponent>(targetID);

            // 死ぬ前に直下の子をルート化(死んだ親IDを子のparentに残さない)。
            // ※プール対象の子はStep1で潜っていないため、ここではフラット化されない。
            if (trans && trans->firstChildID != 0) {
                EntityID childID = trans->firstChildID;
                while (childID != 0) {
                    auto* childTrans = world.GetComponent<TransformComponent>(childID);
                    if (!childTrans) break;
                    EntityID nextChild = childTrans->nextSiblingID;
                    childTrans->parent = 0;
                    childTrans->prevSiblingID = 0;
                    childTrans->nextSiblingID = 0;
                    childTrans->isDirty = true;
                    childID = nextChild;
                }
                trans->firstChildID = 0;
            }

            TransformUpdateSystem::DetachChild(world, targetID);
        }

        world.RequestDestroyEntity(targetID);
    }
}

// =========================================================================
// 子供の再帰的収集 (static)
//   ★プール対象(PooledParticleComponent)には潜らない。配下は emitterRoot と
//     一緒にプールへ返るため、ここで個別に列挙してはならない。
// =========================================================================
void HierarchyCleanupSystem::CollectChildrenRecursive(
    CCL::ECS::World& world, EntityID currentID, std::vector<EntityID>& outList)
{
    auto* trans = world.GetComponent<TransformComponent>(currentID);
    if (!trans) return;

    EntityID childID = trans->firstChildID;
    while (childID != 0) {
        auto* childTrans = world.GetComponent<TransformComponent>(childID);
        if (!childTrans) break;

        outList.push_back(childID);
        EntityID nextSibling = childTrans->nextSiblingID;

        // プール対象なら配下に潜らない(rootごと返却するため列挙不要)。
        if (!world.HasComponent<PooledParticleComponent>(childID)) {
            CollectChildrenRecursive(world, childID, outList);
        }

        childID = nextSibling;
    }
}

REGISTER_LOGIC_SYSTEM(HierarchyCleanupSystem, Priority::LogicStage::L07_Cleanup);