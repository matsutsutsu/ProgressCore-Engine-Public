#include "PrefabSerializer.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Core/SaveContext.h"
#include "Engine/Serialization/Core/SceneFormat.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

#include <json.hpp>
#include <fstream>
#include <iomanip>
#include <queue>
#include <unordered_map>
#include <vector>

using namespace CCL::ECS;
using CCL::Serialization::SaveContext;

namespace
{
    // 全 Entity の TransformComponent.parent から「親 → 子の一覧」の表を作る（走査 1 回）
    using ChildMap = std::unordered_map<EntityID, std::vector<EntityID>>;

    ChildMap BuildParentToChildrenMap(World *world)
    {
        ChildMap result;
        // 再ハッシュを減らすため先に確保する
        result.reserve(256);

        auto &chunkManager = world->GetChunkManager();
        for (const auto &upChunk : chunkManager.GetChunks())
        {
            if (!upChunk) continue;
            Chunk *chunk = upChunk.get();

            size_t          count = chunk->GetEntityCount();
            const EntityID *ids   = chunk->GetEntityIDs();
            for (size_t i = 0; i < count; ++i)
            {
                if (chunk->IsEntityDestroyed(i)) continue;
                EntityID id = ids[i];
                auto    *t  = world->GetComponent<TransformComponent>(id);
                if (t && t->parent != 0 && t->parent != InvalidEntityID)
                {
                    result[t->parent].push_back(id);
                }
            }
        }
        return result;
    }



    // ルートとすべての子孫を幅優先で集める（ルートが先頭）
    std::vector<EntityID> CollectDescendants(EntityID root, const ChildMap &childMap)
    {
        std::vector<EntityID> result;
        std::queue<EntityID>  q;
        q.push(root);
        while (!q.empty())
        {
            EntityID e = q.front();
            q.pop();
            result.push_back(e);

            auto it = childMap.find(e);
            if (it != childMap.end())
            {
                for (EntityID child : it->second)
                    q.push(child);
            }
        }
        return result;
    }
} // anonymous namespace

bool PrefabSerializer::Save(
    const std::string &filepath, World *world, EntityID rootEntity, const std::string &name)
{
    if (!world) return false;
    if (rootEntity == 0 || rootEntity == InvalidEntityID) return false;

    // 1. 親 → 子の表を作る
    ChildMap childMap = BuildParentToChildrenMap(world);

    // 2. ルートとすべての子孫を集める
    std::vector<EntityID> entitiesToSave = CollectDescendants(rootEntity, childMap);
    if (entitiesToSave.empty()) return false;

    // 3. 先に全 Entity にローカルインデックスを振る（後ろの Entity への EntityLink も変換できるように）
    SaveContext saveCtx;
    for (EntityID id : entitiesToSave)
    {
        saveCtx.GetOrRegister(id);
    }

    // 4. JSON を組み立てる
    nlohmann::json root;
    root[CCL::Serialization::Keys::Magic]    = CCL::Serialization::SceneFileMagic;
    root[CCL::Serialization::Keys::Version]  = CCL::Serialization::CurrentSceneVersion;
    root[CCL::Serialization::Keys::Name]     = name;
    root[CCL::Serialization::Keys::Entities] = nlohmann::json::array();

    auto &registry     = ComponentRegistry::Instance();
    auto &chunkManager = world->GetChunkManager();

    size_t savedComponentCount = 0; // 書き出せたコンポーネントの数（ログ用）

    for (EntityID id : entitiesToSave)
    {
        nlohmann::json entityJson;
        entityJson[CCL::Serialization::Keys::EntityLocal] = saveCtx.GetOrRegister(id);
        entityJson[CCL::Serialization::Keys::Components]  = nlohmann::json::object();

        size_t index    = 0;
        size_t chunkIdx = chunkManager.SearchEntityIn(id, &index);
        if (chunkIdx == InvalidIndex)
        {
            // 生成の予約中（ScrutinyAndApply 前）などで実体がまだない
            // 黙って落とすと「保存したのに空」の原因が分からないので必ずログを出す
            CCL_LOG_WARN(LogCategory::Editor,
                         "[Prefab] Entity %llu の実体が見つかりません。保存から除外しました",
                         static_cast<unsigned long long>(id));
            continue;
        }

        Chunk *chunk = chunkManager.GetChunks()[chunkIdx].get();
        for (const auto &typeData : chunk->GetArchetype())
        {
            // ここから下の continue は「このコンポーネントは保存されない」
            //   黙って落とすとプレハブが気付かないうちに壊れるので、理由を必ずログに出す
            //   typeData.name は typeid の型名（例: "struct GPUParticleComponent"）
            const char *typeName = typeData.name ? typeData.name : "(unknown type)";

            std::string cName = registry.GetNameByTypeID(typeData.id);
            if (cName.empty())
            {
                CCL_LOG_WARN(LogCategory::Editor,
                             "[Prefab] %s は ComponentRegistry に未登録のため保存されません "
                             "(REGISTER_COMPONENT を確認)",
                             typeName);
                continue;
            }

            void       *data = chunk->GetComponentPtrByType(typeData.id, index);
            const auto *meta = registry.GetInfoByName(cName);
            if (!meta || !data)
            {
                CCL_LOG_WARN(LogCategory::Editor,
                             "[Prefab] %s (%s) のデータを取得できず保存されません",
                             cName.c_str(), typeName);
                continue;
            }
            if (!meta->isSerializable) continue; // 実行時専用のコンポーネントは保存しない（意図どおりなのでログは出さない）

            nlohmann::json compJson;
            // SaveContext を渡すので EntityLink はローカルインデックスになり、
            // 親子関係もファイルの中で閉じる
            meta->serialize(compJson, data, &saveCtx);
            entityJson[CCL::Serialization::Keys::Components][cName] = std::move(compJson);
            ++savedComponentCount;
        }
        root[CCL::Serialization::Keys::Entities].push_back(std::move(entityJson));
    }

    // 5. ファイルに書き出す
    std::string jsonStr = root.dump(4);
    if (!FileSystem::WriteAllText(filepath, jsonStr))
    {
        CCL_LOG_ERROR(LogCategory::Editor, "[Prefab] %s の書き出しに失敗しました", filepath.c_str());
        return false;
    }

    // 何が保存されたかを 1 行残す。「保存したのに中身が違う」ときに、
    // 選択した Entity を間違えたのかをここで切り分けられる
    CCL_LOG_INFO(LogCategory::Editor,
                 "[Prefab] %s を保存 (エンティティ %zu 個 / コンポーネント %zu 個)",
                 filepath.c_str(), entitiesToSave.size(), savedComponentCount);

    return true;
}