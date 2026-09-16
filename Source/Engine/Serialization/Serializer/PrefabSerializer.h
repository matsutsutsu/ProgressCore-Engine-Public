// =====================================================================
// PrefabSerializer.h
//   特定のルートエンティティ + 子孫を Prefab (JSON) として保存する。
//
//   旧版との違い:
//     - 親子関係探索を O(N²) → O(N) に改善 (キャッシュ構築方式)
//     - SceneSerializer と同じ自動 remap 機構に乗せた (重複コード削減)
//     - 失敗時 false 返却。サイレントな失敗を防ぐ。
// =====================================================================
#pragma once
#include "Engine/ECS/Core/CCL_World.h"
#include <string>

class PrefabSerializer
{
public:
    static bool Save(const std::string &filepath,
                     CCL::ECS::World   *world,
                     CCL::ECS::EntityID rootEntity,
                     const std::string &name);
};