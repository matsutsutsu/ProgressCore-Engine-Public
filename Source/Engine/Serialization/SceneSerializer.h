// Editor/Serialization/SceneSerializer.h
#pragma once
#include "ECS/Core/CCL_World.h"
#include <json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

class SceneSerializer {
  public:
    // シーン全体をファイルに保存
    static void Serialize(CCL::ECS::Core::World *world, const std::string &filepath);

    // ファイルから読み込み (シーンロード、Prefabロード兼用)
    // 戻り値: 生成されたエンティティのIDリスト
    static std::vector<CCL::ECS::EntityID> Deserialize(
        CCL::ECS::Core::World *world, const std::string &filepath);

    // JSONオブジェクトから直接読み込み (LevelLoaderなどで使用)
    static std::vector<CCL::ECS::EntityID> DeserializeRaw(
        CCL::ECS::Core::World *world, const json &entitiesJson);

  private:
    // 内部ヘルパー
    static void SerializeEntity(
        CCL::ECS::Core::World *world, CCL::ECS::EntityID entity, json &outJson);
};