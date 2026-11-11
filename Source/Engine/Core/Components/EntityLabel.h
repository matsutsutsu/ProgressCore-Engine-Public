#pragma once
// ログに出す Entity の表示名（"Player(#12)" など）を作る関数
// 名前は一意でない（ドローンは全機 "Drone"）ので ID も付ける。名前が引けなければ "Entity" で埋める
// Shipping では INFO / WARN のマクロごと消えるので、引数に置いたこの関数も評価されない
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/ECS/Core/CCL_World.h"

#include <string>

namespace CCL
{
    // ログ用の表示名を返す。例: "Player(#12)" / 名前がなければ "Entity(#12)"
    inline std::string EntityLabel(ECS::World &world, ECS::EntityID id)
    {
        const NameComponent *nc   = world.GetComponent<NameComponent>(id);
        const char          *name = (nc && nc->name[0] != '\0') ? nc->name : "Entity";
        return std::string(name) + "(#" + std::to_string(static_cast<unsigned long long>(id)) + ")";
    }
} // namespace CCL
