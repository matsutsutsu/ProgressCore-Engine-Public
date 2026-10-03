#include "SceneSerializer.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Core/SaveContext.h"
#include "Engine/Serialization/Core/LoadContext.h"
#include "Engine/Serialization/Core/SceneFormat.h"

#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Core/Components/PersistentTag.h"
#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Core/Components/PrefabInstanceComponent.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h" // UI は保存しない

#include <json.hpp>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <unordered_set>

#include <tracy/Tracy.hpp>

using namespace CCL::ECS;
using CCL::Serialization::EntityLink;
using CCL::Serialization::LoadContext;
using CCL::Serialization::SaveContext;

namespace
{
    /**
     * @brief root とその子孫の EntityID を深さ優先で集める（root が先頭）。
     *
     * 親子・兄弟のリンクが壊れて循環していても、訪問済みの集合と深さの上限で止まる。
     *
     * @param world   探す World。所有しない。
     * @param id      起点のエンティティ。
     * @param out     集めた EntityID を積む先。
     * @param visited 訪問済みの集合。
     * @param depth   今の深さ。4096 を超えたら止める。
     */
    void CollectCloneSubtree(World *world, EntityID id,
                             std::vector<EntityID>        &out,
                             std::unordered_set<EntityID> &visited, int depth)
    {
        if (depth > 4096) return;               // スタックオーバーフロー対策
        if (!visited.insert(id).second) return; // 訪問済みなら循環している

        out.push_back(id);
        auto *t = world->GetComponent<TransformComponent>(id);
        if (!t) return;

        EntityID child = t->firstChildID;
        while (child != 0)
        {
            if (visited.count(child)) break; // 兄弟リンクが循環している
            auto *ct = world->GetComponent<TransformComponent>(child);
            if (!ct) break;
            EntityID next = ct->nextSiblingID; // 再帰の前に次の兄弟を控える
            CollectCloneSubtree(world, child, out, visited, depth + 1);
            child = next;
        }
    }
} // namespace

// ===========================================================================
// 保存
// ===========================================================================
bool SceneSerializer::Serialize(World *world, const std::string &filepath)
{
    if (!world) return false;

    nlohmann::json root;

    // 読込時に形式とバージョンを判定するためのヘッダ
    root[CCL::Serialization::Keys::Magic]   = CCL::Serialization::SceneFileMagic;
    root[CCL::Serialization::Keys::Version] = CCL::Serialization::CurrentSceneVersion;
    root[CCL::Serialization::Keys::Timestamp] =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    // 1. 保存する Entity を集め、先にローカルインデックスを振る。
    //   A が後ろにある B を参照していても、B の番号が決まっているようにするため
    SaveContext           saveCtx;
    std::vector<EntityID> targets;

    // PrefabInstanceComponent を持つルートとその子孫は保存しない。
    //   レベルファイル側でプレハブの参照として保存されるので、ここでも保存すると二重に配置され、
    //   プレハブの編集も反映されなくなる
    std::unordered_set<EntityID> prefabExcluded;
    {
        auto &chunkManager = world->GetChunkManager();
        for (const auto &upChunk : chunkManager.GetChunks())
        {
            if (!upChunk) continue;
            Chunk *chunk = upChunk.get();
            if (!chunk->HasComponent<PrefabInstanceComponent>()) continue;

            size_t          count = chunk->GetEntityCount();
            const EntityID *ids   = chunk->GetEntityIDs();
            for (size_t i = 0; i < count; ++i)
            {
                if (chunk->IsEntityDestroyed(i)) continue;
                std::vector<EntityID>        sub;
                std::unordered_set<EntityID> visited;
                CollectCloneSubtree(world, ids[i], sub, visited, 0);
                for (EntityID e : sub)
                    prefabExcluded.insert(e);
            }
        }
    }

    {
        auto &chunkManager = world->GetChunkManager();
        for (const auto &upChunk : chunkManager.GetChunks())
        {
            if (!upChunk) continue;
            Chunk *chunk = upChunk.get();

            if (chunk->HasComponent<Tag::DestroyTag>()) continue;    // 削除予定
            if (chunk->HasComponent<Tag::PersistentTag>()) continue; // シーンをまたいで残る Entity
            // UI（HUD・メニュー）は各シーンの System が起動時に生成する。
            // 保存すると、次の起動で「読込分 + System の生成分」の二重になる
            if (chunk->HasComponent<UITransformComponent>()) continue;

            size_t          count = chunk->GetEntityCount();
            const EntityID *ids   = chunk->GetEntityIDs();
            for (size_t i = 0; i < count; ++i)
            {
                if (chunk->IsEntityDestroyed(i)) continue;
                EntityID id = ids[i];
                if (prefabExcluded.count(id)) continue;
                saveCtx.GetOrRegister(id);
                targets.push_back(id);
            }
        }
    }

    // 2. Entity を書き出す
    nlohmann::json entitiesArray = nlohmann::json::array();
    for (EntityID id : targets)
    {
        nlohmann::json entityJson;
        SerializeEntity(world, id, entityJson, saveCtx);
        entitiesArray.push_back(std::move(entityJson));
    }
    root[CCL::Serialization::Keys::Entities] = std::move(entitiesArray);

    // 3. ファイルに書く
    std::string jsonStr = root.dump(4);
    return FileSystem::WriteAllText(filepath, jsonStr);
}

void SceneSerializer::SerializeEntity(
    World *world, EntityID entityID, nlohmann::json &outJson, SaveContext &saveCtx)
{
    // EntityID ではなくローカルインデックスで書く
    outJson[CCL::Serialization::Keys::EntityLocal] = saveCtx.GetOrRegister(entityID);
    outJson[CCL::Serialization::Keys::Components]  = nlohmann::json::object();

    auto &registry = ComponentRegistry::Instance();

    // Entity がいる Chunk の Archetype を順に見て、コンポーネントを書く
    size_t index    = 0;
    size_t chunkIdx = world->GetChunkManager().SearchEntityIn(entityID, &index);
    if (chunkIdx == InvalidIndex) return;

    Chunk      *chunk     = world->GetChunkManager().GetChunks()[chunkIdx].get();
    const auto &archetype = chunk->GetArchetype();

    for (const auto &typeData : archetype)
    {
        std::string name = registry.GetNameByTypeID(typeData.id);
        if (name.empty()) continue;

        void       *data = chunk->GetComponentPtrByType(typeData.id, index);
        const auto *meta = registry.GetInfoByName(name);
        if (!meta || !data) continue;
        if (!meta->isSerializable) continue;

        nlohmann::json compJson;
        // SaveContext を渡すので、EntityLink はローカルインデックスに変換される
        meta->serialize(compJson, data, &saveCtx);
        outJson[CCL::Serialization::Keys::Components][name] = std::move(compJson);
    }
}

// ===========================================================================
// 読込
// ===========================================================================
bool SceneSerializer::LoadEntitiesJson(const std::string &filepath, nlohmann::json &outEntities)
{
    ZoneScopedN("SceneSerializer::LoadEntitiesJson");

    std::string jsonStr;
    {
        ZoneScopedN("SceneSerializer: ReadAllText (disk)");
        jsonStr = FileSystem::ReadAllText(filepath);
    }
    if (jsonStr.empty())
    {
        return false;
    }

    nlohmann::json root;
    try
    {
        ZoneScopedN("SceneSerializer: json::parse");
        root = nlohmann::json::parse(jsonStr);
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Core,
                      "[SceneSerializer] JSON parse failed (%s): %s", filepath.c_str(), e.what());
        return false;
    }

    // 形式の検証（magic が無いファイルは読まない）
    if (!root.contains(CCL::Serialization::Keys::Magic))
    {
        CCL_LOG_ERROR(LogCategory::Core,
                      "[SceneSerializer] missing magic — not a valid scene/prefab file: %s", filepath.c_str());
        return false;
    }
    {
        uint32_t magic = root[CCL::Serialization::Keys::Magic].get<uint32_t>();
        if (magic != CCL::Serialization::SceneFileMagic)
        {
            CCL_LOG_ERROR(LogCategory::Core,
                          "[SceneSerializer] magic mismatch: %s", filepath.c_str());
            return false;
        }
        uint32_t ver = root.value(CCL::Serialization::Keys::Version, 0u);
        if (ver < CCL::Serialization::MinSupportedSceneVersion)
        {
            CCL_LOG_ERROR(LogCategory::Core,
                          "[SceneSerializer] unsupported version %u: %s", ver, filepath.c_str());
            return false;
        }
    }

    if (!root.contains(CCL::Serialization::Keys::Entities)) return false;

    outEntities = std::move(root[CCL::Serialization::Keys::Entities]);
    return true;
}

std::vector<EntityID> SceneSerializer::Deserialize(World *world, const std::string &filepath)
{
    ZoneScopedN("SceneSerializer::Deserialize");

    nlohmann::json entities;
    if (!LoadEntitiesJson(filepath, entities)) return {};

    return DeserializeRaw(world, entities);
}

std::vector<EntityID> SceneSerializer::DeserializeRaw(World *world, const nlohmann::json &entitiesJson)
{
    if (!world || !entitiesJson.is_array()) return {};

    std::vector<EntityID> createdEntities;
    createdEntities.reserve(entitiesJson.size());

    LoadContext loadCtx;
    loadCtx.Reserve(entitiesJson.size());

    auto &registry = ComponentRegistry::Instance();

    // 1. 全 Entity の生成を予約し、「ローカルインデックス → 新しい EntityID」を登録する。
    //   コンポーネントはまだ付けない（EntityLink の変換先を先に全て決めるため）
    Archetype emptyArch;
    for (const auto &entityJson : entitiesJson)
    {
        EntityID newID = world->RequestSpawnEntity(emptyArch);
        createdEntities.push_back(newID);

        if (entityJson.contains(CCL::Serialization::Keys::EntityLocal))
        {
            uint32_t localIdx = entityJson[CCL::Serialization::Keys::EntityLocal].get<uint32_t>();
            loadCtx.Register(localIdx, newID);
        }
        else
        {
            CCL_LOG_ERROR(LogCategory::Core,
                          "[SceneSerializer] entity missing local index (lid) — link/parent resolution may fail");
        }
    }

    // 2. コンポーネントを復元する（AddComponent は予約として積まれる）。
    //   EntityLink はここで LoadContext により新しい EntityID に変換される
    {
        size_t i = 0;
        for (const auto &entityJson : entitiesJson)
        {
            EntityID newID = createdEntities[i++];
            if (!entityJson.contains(CCL::Serialization::Keys::Components)) continue;

            const auto &components = entityJson[CCL::Serialization::Keys::Components];
            for (auto it = components.begin(); it != components.end(); ++it)
            {
                const std::string &compName = it.key();
                const auto        &compJson = it.value();
                const auto        *meta     = registry.GetInfoByName(compName);
                if (!meta)
                {
                    // 登録名と綴りが違うキー。黙って捨てると「プレハブに書いたのに付いていない」
                    // 原因が分からないので警告を出す
                    CCL_LOG_WARN(LogCategory::Core,
                                 "[Scene] コンポーネント '%s' は ComponentRegistry に未登録のため無視されました"
                                 "（登録名の綴り違い／未登録のメタ定義を確認）",
                                 compName.c_str());
                    continue;
                }
                meta->deserializeToWorld(world, newID, compJson, &loadCtx);
            }
        }
    }

    // 3. 予約した生成と AddComponent を確定させる
    world->ScrutinyAndApply();

    // 4. 親子のリンクを張る。
    //   parent は 2 で新しい EntityID になっているが、保存しない子・兄弟のリンクはまだ無いので、
    //   SetParent で双方向に張り直す。全 Entity が実体化済みなので順番は問わない
    for (EntityID id : createdEntities)
    {
        auto *t = world->GetComponent<TransformComponent>(id);
        if (!t)
            continue;
        if (!t->parent.IsValid())
            continue; // 親なし
        TransformUpdateSystem::SetParent(*world, id, t->parent.id);
    }

    return createdEntities;
}

// ===========================================================================
// 複製（エディタ用。保存と読込をメモリ上だけで行う）
// ===========================================================================
std::vector<EntityID> SceneSerializer::DuplicateHierarchy(World *world, EntityID root)
{
    if (!world || root == InvalidEntityID) return {};

    // 1. root とその子孫を集める（root が先頭）
    std::vector<EntityID> subtree;
    {
        std::unordered_set<EntityID> visited;
        CollectCloneSubtree(world, root, subtree, visited, 0);
    }
    if (subtree.empty()) return {};

    // 2. 先に全 Entity にローカルインデックスを振る（子孫の中の参照を正しく変換するため）
    SaveContext saveCtx;
    for (EntityID id : subtree)
        saveCtx.GetOrRegister(id);

    // 3. JSON 配列に書き出す
    nlohmann::json entitiesArray = nlohmann::json::array();
    for (EntityID id : subtree)
    {
        nlohmann::json entityJson;
        SerializeEntity(world, id, entityJson, saveCtx);
        entitiesArray.push_back(std::move(entityJson));
    }

    // 4. 読み込んで新しい Entity を生成する
    std::vector<EntityID> created = DeserializeRaw(world, entitiesArray);
    if (created.empty()) return {};

    EntityID newRoot = created[0];

    // 5. 複製のルートを元と同じ親の子にする（子孫の外にある親は変換されず、親なしになっているため）
    if (auto *srcT = world->GetComponent<TransformComponent>(root))
    {
        if (srcT->parent.IsValid())
        {
            TransformUpdateSystem::SetParent(*world, newRoot, srcT->parent.id);
        }
    }

    // 6. 複製のルートの名前に " (Copy)" を付ける
    if (auto *nc = world->GetComponent<NameComponent>(newRoot))
    {
        std::string newName = std::string(nc->name) + " (Copy)";
        strncpy_s(nc->name, newName.c_str(), sizeof(nc->name) - 1);
    }

    return created;
}