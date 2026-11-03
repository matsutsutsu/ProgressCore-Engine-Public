#pragma once
// ===========================================================================
// 概要: エディタで組んだ UI プレハブを、ゲームの System から使うための関数をまとめたヘッダー。
//
// 責務:
//   - 生成したプレハブの階層から、名前で子のエンティティを探す
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <cstring>

namespace CCL::GameUI
{
    // parent の子孫から、名前が name のエンティティを探す（見つからなければ InvalidEntityID）
    inline CCL::ECS::EntityID FindChildByName(CCL::ECS::World &world, CCL::ECS::EntityID parent, const char *name)
    {
        const auto *parentT = world.GetComponent<TransformComponent>(parent);
        if (!parentT) return CCL::ECS::InvalidEntityID;

        // 子の連結リストを先頭からたどり、見つからなければその子の下も探す
        for (CCL::ECS::EntityID child = parentT->firstChildID; child != 0;)
        {
            const auto *n = world.GetComponent<NameComponent>(child);
            if (n && std::strcmp(n->name, name) == 0) return child;

            const CCL::ECS::EntityID found = FindChildByName(world, child, name);
            if (found != CCL::ECS::InvalidEntityID) return found;

            const auto *t = world.GetComponent<TransformComponent>(child);
            child         = t ? t->nextSiblingID : 0;
        }
        return CCL::ECS::InvalidEntityID;
    }
} // namespace CCL::GameUI
