#include "HierarchyCleanupSystem.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <algorithm>
#include <vector>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

HierarchyCleanupSystem::HierarchyCleanupSystem() : IfSystem("HierarchyCleanupSystem") {}

void HierarchyCleanupSystem::Update(float dt)
{
    ExecuteCleanup(*m_world);
}

void HierarchyCleanupSystem::CollectDoomedEntities(
    CCL::ECS::World &world, std::vector<EntityID> &outList)
{
    // 1. DestroyTag を持つ「削除の起点」を見つけ、子孫も集める。
    //    static 関数なので ForEach は使えず、Chunk を直接走査する
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

    // 2. 起点どうしが親子の場合に同じ ID が2回入るので、重複を除く
    std::sort(outList.begin(), outList.end());
    outList.erase(std::unique(outList.begin(), outList.end()), outList.end());
}

void HierarchyCleanupSystem::ExecuteCleanup(CCL::ECS::World &world)
{
    std::vector<EntityID> executionList;
    executionList.reserve(64);

    CollectDoomedEntities(world, executionList);

    for (EntityID targetID : executionList)
    {
        if (world.HasComponent<TransformComponent>(targetID))
        {
            auto *trans = world.GetComponent<TransformComponent>(targetID);

            // 破棄する前に直下の子をルートにする（死んだ親の ID を子の parent に残さない）
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
        EntityID nextSibling = childTrans->nextSiblingID; // 再帰の前に次の兄弟を控えておく

        CollectChildrenRecursive(world, childID, outList);

        childID = nextSibling;
    }
}

REGISTER_LOGIC_SYSTEM(HierarchyCleanupSystem, Priority::LogicStage::L07_Cleanup);