#include "LevelSerializer.h"

#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Components/PrefabInstanceComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <json.hpp>
#include <DirectXMath.h>
#include <filesystem>
#include <vector>

using namespace CCL::ECS;
namespace fs = std::filesystem;

// =====================================================================
// 保存
// =====================================================================
bool LevelSerializer::SaveLevel(World *world, const std::string &levelPath)
{
    if (!world) return false;

    // 生成・破棄の予約を確定させてから保存する（途中の状態の Entity を混ぜないため）
    world->ScrutinyAndApply();

    // level ファイルの隣に <stem>.scene.json を書く
    fs::path    lp(levelPath);
    std::string stem = lp.stem().string();
    // "Foo.level.json" のような二重拡張子なら ".level" を外す
    const std::string levelSuffix = ".level";
    if (stem.size() > levelSuffix.size() &&
        stem.compare(stem.size() - levelSuffix.size(), levelSuffix.size(), levelSuffix) == 0)
    {
        stem = stem.substr(0, stem.size() - levelSuffix.size());
    }

    const std::string sceneRel  = stem + ".scene.json";
    const fs::path    scenePath = lp.parent_path() / sceneRel;

    // ダイアログが拡張子なしや .json を返しても、必ず <stem>.level.json と <stem>.scene.json の組で書く
    const fs::path levelOut = lp.parent_path() / (stem + ".level.json");

    // 1. ステージを保存する（プレハブの Entity は SceneSerializer が除外する）
    if (!SceneSerializer::Serialize(world, scenePath.string()))
    {
        CCL_LOG_ERROR(LogCategory::Core,
                      "[Level] ステージ保存に失敗: %s", scenePath.string().c_str());
        return false;
    }

    // 2. プレハブの配置を集める
    nlohmann::json instances   = nlohmann::json::array();
    int            placedCount = 0;

    auto &chunkMgr = world->GetChunkManager();
    for (auto &upChunk : chunkMgr.GetChunks())
    {
        if (!upChunk) continue;
        Chunk *chunk = upChunk.get();
        if (!chunk->HasComponent<PrefabInstanceComponent>()) continue;

        PrefabInstanceComponent *insts = chunk->GetComponentArray<PrefabInstanceComponent>();
        const EntityID          *ids   = chunk->GetEntityIDs();
        const size_t             count = chunk->GetEntityCount();

        for (size_t i = 0; i < count; ++i)
        {
            if (chunk->IsEntityDestroyed(i)) continue;
            const std::string &path = insts[i].prefabPath;
            if (path.empty()) continue; // パスを持つのはプレハブのルートだけ

            nlohmann::json e;
            e["prefab"] = path;
            if (auto *t = world->GetComponent<TransformComponent>(ids[i]))
            {
                e["position"] = {t->position.x, t->position.y, t->position.z};
                e["rotation"] = {t->rotation.x, t->rotation.y, t->rotation.z, t->rotation.w};
                e["scale"]    = {t->scale.x, t->scale.y, t->scale.z};
            }
            instances.push_back(std::move(e));
            ++placedCount;
        }
    }

    // 3. level ファイルを書く
    nlohmann::json root;
    root["type"]      = "level";
    root["version"]   = 1;
    root["scene"]     = sceneRel; // level ファイルからの相対パス
    root["instances"] = std::move(instances);

    if (!FileSystem::WriteAllText(levelOut.string(), root.dump(4)))
    {
        CCL_LOG_ERROR(LogCategory::Core, "[Level] レベル保存に失敗: %s", levelOut.string().c_str());
        return false;
    }

    CCL_LOG_INFO(LogCategory::Core,
                 "[Level] 保存完了: %s (scene=%s, prefab=%d)", levelOut.string().c_str(), sceneRel.c_str(), placedCount);
    return true;
}



// =====================================================================
// ロード
// =====================================================================
bool LevelSerializer::LoadLevel(World *world, const std::string &levelPath)
{
    if (!world) return false;

    const std::string jsonStr = FileSystem::ReadAllText(levelPath);
    if (jsonStr.empty())
    {
        CCL_LOG_ERROR(LogCategory::Core,
                      "[Level] 読み込み中止: ファイルが無いか空です (%s)", levelPath.c_str());
        return false;
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(jsonStr);
    }
    catch (const std::exception &ex)
    {
        CCL_LOG_ERROR(LogCategory::Core, "[Level] JSON parse 失敗 (%s): %s", levelPath.c_str(), ex.what());
        return false;
    }

    const std::string sceneRel = root.value("scene", std::string{});
    if (sceneRel.empty())
    {
        CCL_LOG_ERROR(LogCategory::Core, "[Level] scene 参照がありません: %s", levelPath.c_str());
        return false;
    }

    fs::path       lp(levelPath);
    const fs::path scenePath = lp.parent_path() / sceneRel;

    // 1. ステージを読み込む（World をすべて消して差し替える）
    SceneManager::Instance().LoadScene(world, scenePath.string());

    // 2. プレハブを生成し直す
    int spawned = 0;
    if (root.contains("instances") && root["instances"].is_array())
    {
        for (const auto &e : root["instances"])
        {
            const std::string prefab = e.value("prefab", std::string{});
            if (prefab.empty()) continue;

            DirectX::XMFLOAT3 pos{0, 0, 0};
            DirectX::XMFLOAT4 rot{0, 0, 0, 1};
            DirectX::XMFLOAT3 scl{1, 1, 1};

            if (e.contains("position") && e["position"].is_array() && e["position"].size() == 3)
            {
                pos = {e["position"][0].get<float>(), e["position"][1].get<float>(), e["position"][2].get<float>()};
            }
            if (e.contains("rotation") && e["rotation"].is_array() && e["rotation"].size() == 4)
            {
                rot = {e["rotation"][0].get<float>(), e["rotation"][1].get<float>(),
                       e["rotation"][2].get<float>(), e["rotation"][3].get<float>()};

                // 長さが1でないクォータニオンは、行列にしたとき長さの2乗で拡大される
                // JSON を手で書き換えて角度（度）を入れても、モデルが膨らまないよう正規化する
                const DirectX::XMVECTOR q = DirectX::XMLoadFloat4(&rot);
                if (DirectX::XMVectorGetX(DirectX::XMVector4LengthSq(q)) > 1e-8f)
                {
                    DirectX::XMStoreFloat4(&rot, DirectX::XMQuaternionNormalize(q));
                }
                else
                {
                    rot = {0, 0, 0, 1};
                }
            }
            if (e.contains("scale") && e["scale"].is_array() && e["scale"].size() == 3)
            {
                scl = {e["scale"][0].get<float>(), e["scale"][1].get<float>(), e["scale"][2].get<float>()};
            }

            EntityID rootId = Prefab::SpawnPrefab(*world, prefab, pos, rot);
            if (rootId == InvalidEntityID)
            {
                CCL_LOG_WARN(LogCategory::Core, "[Level] プレハブ生成に失敗: %s", prefab.c_str());
                continue;
            }
            // スケールを適用し、次にレベルを保存するときも参照で保存されるよう PrefabInstanceComponent を付ける
            world->PatchComponent<TransformComponent>(rootId, [scl](TransformComponent &t) { t.scale = scl; });
            world->AddComponent<PrefabInstanceComponent>(rootId, PrefabInstanceComponent{prefab});
            ++spawned;
        }
    }

    world->ScrutinyAndApply();
    CCL_LOG_INFO(LogCategory::Core,
                 "[Level] 読み込み完了: %s (scene=%s, prefab=%d)", levelPath.c_str(), sceneRel.c_str(), spawned);
    return true;
}
