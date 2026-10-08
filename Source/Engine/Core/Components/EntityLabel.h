#pragma once
// ===========================================================================
// 概要: ログに出す Entity の表示名（"Player(#12)" など）を作るヘルパー。
//
// 責務:
//   - NameComponent の名前と EntityID を組み合わせた表示名を返す
//
// 備考: 設計方針
//   - 生の EntityID だけを出すログは、読んだ人が「誰の話か」を判断できない。
//     NameComponent があれば名前を頭に付け、ログ単体で意味が通るようにする。
//   - 名前だけにせず ID も併記する。名前は一意ではない（ドローンは1000機すべて
//     "Drone"）ので、ID を落とすとどの個体の話か追えなくなる。
//   - 名前が引けないとき（破棄済み・生成予約中）は "Entity" で埋める。
//     ログのために nullptr 分岐を呼び出し側へ書かせない。
//   - INFO/SUCCESS/WARN は Release でマクロごと ((void)0) に消えるため、
//     引数に置いたこの関数も評価されない。製品ビルドでの文字列生成は発生しない。
// ===========================================================================
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/ECS/Core/CCL_World.h"

#include <string>

namespace CCL
{
    // ログ用の表示名を返す。例: "Player(#12)" / 名前が無ければ "Entity(#12)"。
    inline std::string EntityLabel(ECS::World &world, ECS::EntityID id)
    {
        const NameComponent *nc   = world.GetComponent<NameComponent>(id);
        const char          *name = (nc && nc->name[0] != '\0') ? nc->name : "Entity";
        return std::string(name) + "(#" + std::to_string(static_cast<unsigned long long>(id)) + ")";
    }
} // namespace CCL
