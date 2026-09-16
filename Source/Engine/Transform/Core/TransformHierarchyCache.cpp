#include "TransformHierarchyCache.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include <algorithm>

using namespace CCL::ECS;
using namespace CCL::ECS;

namespace
{
    struct StackEntry
    {
        EntityID id;
        int      parentOrder;
        EntityID parentId;
    };
} // namespace

void TransformHierarchyCache::Rebuild(World &world)
{
    std::vector<EntityID> all = world.View<TransformComponent>();

    uint32_t maxIndex = 0;
    for (EntityID id : all)
        maxIndex = (std::max)(maxIndex, EntityHandle::GetIndex(id));
    m_idToOrder.assign(maxIndex + 1, -1);

    m_order.clear();
    m_parentInOrder.clear();
    m_parentId.clear();

    std::vector<StackEntry> stack;
    stack.reserve(64);

    // 各ルートの「子」から DFS 開始（ルート自身は記録しない）
    for (EntityID rootId : all)
    {
        auto *rt = world.GetComponent<TransformComponent>(rootId);
        if (!rt) continue;
        if (static_cast<EntityID>(rt->parent) != 0) continue; // 起点はルートのみ

        EntityID child = rt->firstChildID;
        while (child != 0)
        {
            auto *ct = world.GetComponent<TransformComponent>(child);
            if (!ct) break;
            const EntityID next = ct->nextSiblingID;
            stack.push_back({child, -1, rootId}); // 親はルート rootId
            child = next;
        }

        while (!stack.empty())
        {
            const StackEntry e = stack.back();
            stack.pop_back();

            const uint32_t pure = EntityHandle::GetIndex(e.id);
            if (pure < m_idToOrder.size() && m_idToOrder[pure] != -1) continue; // 循環/重複ガード

            auto *t = world.GetComponent<TransformComponent>(e.id);
            if (!t) continue;

            const int myOrder = static_cast<int>(m_order.size());
            m_order.push_back(e.id);
            m_parentInOrder.push_back(e.parentOrder);
            m_parentId.push_back(e.parentId);
            if (pure < m_idToOrder.size()) m_idToOrder[pure] = myOrder;

            EntityID gc = t->firstChildID;
            while (gc != 0)
            {
                auto *gct = world.GetComponent<TransformComponent>(gc);
                if (!gct) break;
                const EntityID next = gct->nextSiblingID;
                stack.push_back({gc, myOrder, e.id}); // 親は非ルート（dense から読む）
                gc = next;
            }
        }
    }

    // orphan sweep：DFS で拾えなかった非ルートを救済（親worldはコンポーネントから best-effort）
    for (EntityID id : all)
    {
        auto *t = world.GetComponent<TransformComponent>(id);
        if (!t) continue;
        if (static_cast<EntityID>(t->parent) == 0) continue; // ルートは対象外
        const uint32_t pure = EntityHandle::GetIndex(id);
        if (pure < m_idToOrder.size() && m_idToOrder[pure] != -1) continue;
        const int myOrder = static_cast<int>(m_order.size());
        m_order.push_back(id);
        m_parentInOrder.push_back(-1);
        m_parentId.push_back(static_cast<EntityID>(t->parent));
        m_idToOrder[pure] = myOrder;
    }

    m_worldScratch.assign(m_order.size(), DirectX::XMFLOAT4X4{});
}