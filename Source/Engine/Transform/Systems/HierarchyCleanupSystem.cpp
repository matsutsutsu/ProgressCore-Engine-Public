// ===========================================================================
// HierarchyCleanupSystem.cpp
// --------------------------
// 役割:
//   DestroyTag を起点に、死亡Entityとその子階層を安全に回収するL07システム。
//
// 実装の意図(Why):
//   - 反復中の構造変更は危険なため、まず「誰を消すか」を executionList に確定
//     させてから一括処理する。
// ===========================================================================
#include "HierarchyCleanupSystem.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <algorithm>
#include <vector>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace CCL::ECS;

HierarchyCleanupSystem::HierarchyCleanupSystem() : IfSystem("HierarchyCleanupSystem") {}

void HierarchyCleanupSystem::Update(float dt)
{
    ExecuteCleanup(*m_world);
}

// =========================================================================
// 実際のクリーンアップ処理 (static)
// =========================================================================
void HierarchyCleanupSystem::CollectDoomedEntities(
    CCL::ECS::World &world, std::vector<EntityID> &outList)
{
    // 1. DestroyTag を持つ「削除起点」を見つけ、子も収集する(チャンク手動走査)。
    auto &chunks = world.GetChunkManager().GetChunks();
    for (auto &chunkPtr : chunks)
    {
        if (!chunkPtr) continue;
        auto *chunk = chunkPtr.get();
        if (!chunk->HasComponent<Tag::DestroyTag>()) continue;

        size_t          count     = chunk->GetEntityCount();
        const EntityID *entityIDs = chunk->GetEntityIDs();

        for (size_t i = 0; i < count; ++i)
        {
            if (chunk->IsEntityDestroyed(i)) continue;
            EntityID id = entityIDs[i];
            outList.push_back(id);
            if (world.HasComponent<TransformComponent>(id))
            {
                CollectChildrenRecursive(world, id, outList);
            }
        }
    }

    // 2. 重複排除。
    std::sort(outList.begin(), outList.end());
    outList.erase(std::unique(outList.begin(), outList.end()), outList.end());
}

void HierarchyCleanupSystem::ExecuteCleanup(CCL::ECS::World &world)
{
    std::vector<EntityID> executionList;
    executionList.reserve(64);

    CollectDoomedEntities(world, executionList);

    // 3. 削除の実行。
    for (EntityID targetID : executionList)
    {

        if (world.HasComponent<TransformComponent>(targetID))
        {
            auto *trans = world.GetComponent<TransformComponent>(targetID);

            // 死ぬ前に直下の子をルート化(死んだ親IDを子のparentに残さない)。
            if (trans && trans->firstChildID != 0)
            {
                EntityID childID = trans->firstChildID;
                while (childID != 0)
                {
                    auto *childTrans = world.GetComponent<TransformComponent>(childID);
                    if (!childTrans) break;
                    EntityID nextChild        = childTrans->nextSiblingID;
                    childTrans->parent        = 0;
                    childTrans->prevSiblingID = 0;
                    childTrans->nextSiblingID = 0;
                    childTrans->isDirty       = true;
                    childID                   = nextChild;
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
// =========================================================================
void HierarchyCleanupSystem::CollectChildrenRecursive(
    CCL::ECS::World &world, EntityID currentID, std::vector<EntityID> &outList)
{
    auto *trans = world.GetComponent<TransformComponent>(currentID);
    if (!trans) return;

    EntityID childID = trans->firstChildID;
    while (childID != 0)
    {
        auto *childTrans = world.GetComponent<TransformComponent>(childID);
        if (!childTrans) break;

        outList.push_back(childID);
        EntityID nextSibling = childTrans->nextSiblingID; // 再帰前に次兄弟を退避

        CollectChildrenRecursive(world, childID, outList);

        childID = nextSibling;
    }
}

REGISTER_LOGIC_SYSTEM(HierarchyCleanupSystem, Priority::LogicStage::L07_Cleanup);