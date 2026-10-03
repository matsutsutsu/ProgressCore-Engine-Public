#pragma once
// ===========================================================================
// 概要: ルートの Entity とその子孫をプレハブ（JSON）として保存するクラス PrefabSerializer。
//
// 責務:
//   - ルートから子孫を集め、ローカルインデックスを振って保存する
//
// 備考: 保存されないコンポーネント（未登録・取得失敗）は理由をログに出す。
//       形式は SceneSerializer と同じなので、Prefab::SpawnPrefab で読める。
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include <string>

class PrefabSerializer
{
public:
    /**
     * @brief rootEntity とその子孫をプレハブのファイルとして保存する。
     * @param filepath   保存先のファイル。
     * @param world      保存する Entity がいる World。
     * @param rootEntity プレハブのルートにする Entity。
     * @param name       プレハブ名（ファイルの name に書く）。
     * @return 保存できたら true。world が無い・rootEntity が無効・保存する Entity が無いときは false。
     */
    static bool Save(const std::string &filepath,
                     CCL::ECS::World   *world,
                     CCL::ECS::EntityID rootEntity,
                     const std::string &name);
};