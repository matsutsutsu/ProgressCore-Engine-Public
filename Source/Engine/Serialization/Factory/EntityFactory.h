#pragma once
// 名前付きの Entity やプレハブを生成して EntityRef で返す関数
// NameComponent を付ける処理をここに置き、ECS のコアに NameComponent を持ち込まない
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Core/Components/NameComponent.h"

namespace EntityFactory
{

    // Archetype と名前から、NameComponent を付けた Entity を生成する
    inline CCL::ECS::EntityRef SpawnNamed(CCL::ECS::World           &world,
                                          const char                *name,
                                          const CCL::ECS::Archetype &baseArch)
    {
        auto ref = world.Spawn(baseArch);

        // NameComponent が Archetype になければ、追加が予約される
        ref.Set(NameComponent{name});

        return ref;
    }

    // プレハブを生成し、ルートの EntityRef を返す
    inline CCL::ECS::EntityRef SpawnPrefab(CCL::ECS::World &world,
                                           const char      *prefabPath)
    {
        CCL::ECS::EntityID id = Prefab::SpawnPrefab(world, prefabPath);
        return {&world, id};
    }

} // namespace EntityFactory