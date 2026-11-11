#pragma once
// ===========================================================================
// 概要: ルートの Entity とその子孫をプレハブ（JSON）として保存するクラス PrefabSerializer
//
// 責務:
//   - ルートから子孫を集め、ローカルインデックスを振って保存する
//
// 備考: 保存されないコンポーネント（未登録・取得失敗）は理由をログに出す
//       形式は SceneSerializer と同じなので、Prefab::SpawnPrefab で読める
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include <string>

class PrefabSerializer
{
public:
    // rootEntity とその子孫を、プレハブのファイルとして保存する。name はファイルの name に書く
    // world がない・rootEntity が無効・保存する Entity がないときは false
    static bool Save(const std::string &filepath,
                     CCL::ECS::World   *world,
                     CCL::ECS::EntityID rootEntity,
                     const std::string &name);
};