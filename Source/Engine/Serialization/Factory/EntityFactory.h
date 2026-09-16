#pragma once
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Core/Components/NameComponent.h"

// ゲームレイヤー専用のユーティリティ名前空間
namespace EntityFactory
{

    // ========================================================================
    //  名前付き生成のヘルパー。NameComponent の付与を ECS コア側に持ち込まないための層。
    // ========================================================================
    // アーキタイプと名前を渡すと、NameComponent を付けたエンティティを生成して返す。
    inline CCL::ECS::EntityRef SpawnNamed(CCL::ECS::World           &world,
                                          const char                *name,
                                          const CCL::ECS::Archetype &baseArch)
    {
        // 1. World の純粋な機能を使ってエンティティを生成
        auto ref = world.Spawn(baseArch);

        // 2. 名前をセット（NameComponentを持っていなければ、自動的に追加キューに乗る）
        ref.Set(NameComponent{name});

        return ref;
    }

    // プレハブをSpawnして、便利な EntityRef を返す
    inline CCL::ECS::EntityRef SpawnPrefab(CCL::ECS::World &world,
                                           const char      *prefabPath)
    {
        CCL::ECS::EntityID id = Prefab::SpawnPrefab(world, prefabPath);
        return {&world, id};
    }

} // namespace EntityFactory