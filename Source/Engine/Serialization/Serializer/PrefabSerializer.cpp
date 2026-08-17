// =====================================================================
// PrefabSerializer.cpp
//   旧版の致命的問題を解消した実装。
//
//   解消した点:
//     [旧] BFS 中に毎ステップで全リンク配列を線形検索 → O(N^2)
//     → [新] 親→子のマルチマップを事前構築 → 全体 O(N)
//
//     [旧] SceneSerializer と同じロジックを重複実装
//     → [新] SerializeEntity と同等の処理を共通化 (registry 経由)
//
//     [旧] raw EntityID 保存
//     → [新] SaveContext でローカルインデックス化
//
//   将来の拡張余地:
//     - 親→子キャッシュは HierarchySystem で常時保持するのがベスト。
//       そうすれば O(1) で子取得できる。
//       現状は保存時のみ一時構築するが、これだけでも実用上は十分速い。
// =====================================================================
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
using namespace CCL::ECS;
using CCL::Serialization::SaveContext;

namespace {
    // -----------------------------------------------------------------
    // 親→子のマルチマップを構築する (走査 1 回 = O(N))
    //
    // 注意: TransformComponent しか親子情報を持たないため、ここを汎用化したい場合は
    //       Hierarchy 専用コンポーネント (children list) を将来追加するのがベスト。
    //       現状でも実用上は十分速い (1万エンティティで数ミリ秒以内)。
    // -----------------------------------------------------------------
    using ChildMap = std::unordered_map<EntityID, std::vector<EntityID>>;

    ChildMap BuildParentToChildrenMap(World* world)
    {
        ChildMap result;
        // 概算で hash 衝突を減らすため reserve しておく
        result.reserve(256);

        auto& chunkManager = world->GetChunkManager();
        for (const auto& upChunk : chunkManager.GetChunks()) {
            if (!upChunk) continue;
            Chunk* chunk = upChunk.get();

            size_t          count = chunk->GetEntityCount();
            const EntityID* ids = chunk->GetEntityIDs();
            for (size_t i = 0; i < count; ++i) {
                if (chunk->IsEntityDestroyed(i)) continue;
                EntityID id = ids[i];
                auto* t = world->GetComponent<TransformComponent>(id);
                if (t && t->parent != 0 && t->parent != InvalidEntityID) {
                    result[t->parent].push_back(id);
                }
            }
        }
        return result;
    }

    // -----------------------------------------------------------------
    // ルートから始まる全子孫を集める (BFS = O(N))
    // -----------------------------------------------------------------
    std::vector<EntityID> CollectDescendants(EntityID root, const ChildMap& childMap)
    {
        std::vector<EntityID> result;
        std::queue<EntityID> q;
        q.push(root);
        while (!q.empty()) {
            EntityID e = q.front(); q.pop();
            result.push_back(e);

            auto it = childMap.find(e);
            if (it != childMap.end()) {
                for (EntityID child : it->second) q.push(child);
            }
        }
        return result;
    }
} // anonymous namespace

bool PrefabSerializer::Save(
    const std::string& filepath, World* world, EntityID rootEntity, const std::string& name)
{
    if (!world) return false;
    if (rootEntity == 0 || rootEntity == InvalidEntityID) return false;

    // ---- 1. 親子キャッシュ構築 (O(N)) ----
    ChildMap childMap = BuildParentToChildrenMap(world);

    // ---- 2. ルート + 全子孫を収集 (O(N)) ----
    std::vector<EntityID> entitiesToSave = CollectDescendants(rootEntity, childMap);
    if (entitiesToSave.empty()) return false;

    // ---- 3. ローカルインデックス事前発行 ----
    // EntityLink の解決に必要なので、書き出し前に全エンティティを登録しておく。
    SaveContext saveCtx;
    for (EntityID id : entitiesToSave) {
        saveCtx.GetOrRegister(id);
    }

    // ---- 4. JSON 構築 ----
    nlohmann::json root;
    root[CCL::Serialization::Keys::Magic] = CCL::Serialization::SceneFileMagic;
    root[CCL::Serialization::Keys::Version] = CCL::Serialization::CurrentSceneVersion;
    root[CCL::Serialization::Keys::Name] = name;
    root[CCL::Serialization::Keys::Entities] = nlohmann::json::array();

    auto& registry = ComponentRegistry::Instance();
    auto& chunkManager = world->GetChunkManager();

    size_t savedComponentCount = 0;   // 実際に書き出せたコンポーネント総数（診断用）

    for (EntityID id : entitiesToSave) {
        nlohmann::json entityJson;
        entityJson[CCL::Serialization::Keys::EntityLocal] = saveCtx.GetOrRegister(id);
        entityJson[CCL::Serialization::Keys::Components] = nlohmann::json::object();

        size_t index = 0;
        size_t chunkIdx = chunkManager.SearchEntityIn(id, &index);
        if (chunkIdx == InvalidIndex) {
            // 生成予約中(ScrutinyAndApply 前)などで実体がまだ無い。黙って落とすと
            // 「保存したのに空」の原因が分からなくなるので必ず知らせる。
            CCL_LOG_WARN(LogCategory::Editor,
                "[Prefab] Entity %llu の実体が見つかりません。保存から除外しました",
                static_cast<unsigned long long>(id));
            continue;
        }

        Chunk* chunk = chunkManager.GetChunks()[chunkIdx].get();
        for (const auto& typeData : chunk->GetArchetype()) {
            // ここから下の continue は「このコンポーネントは保存されない」を意味する。
            //   無言で落とすとプレハブが静かに壊れるので、理由を必ずログに出す。
            //   ※ typeData.name は typeid ベースの型名 (例: "struct GPUParticleComponent")。
            const char* typeName = typeData.name ? typeData.name : "(unknown type)";

            std::string cName = registry.GetNameByTypeID(typeData.id);
            if (cName.empty()) {
                CCL_LOG_WARN(LogCategory::Editor,
                    "[Prefab] %s は ComponentRegistry に未登録のため保存されません "
                    "(REGISTER_COMPONENT を確認)", typeName);
                continue;
            }

            void* data = chunk->GetComponentPtrByType(typeData.id, index);
            const auto* meta = registry.GetInfoByName(cName);
            if (!meta || !data) {
                CCL_LOG_WARN(LogCategory::Editor,
                    "[Prefab] %s (%s) のデータを取得できず保存されません",
                    cName.c_str(), typeName);
                continue;
            }
            if (!meta->isSerializable) continue;   // 実行時専用。設計通りなので無言でよい

            nlohmann::json compJson;
            // SaveContext を渡すことで EntityLink が自動で localIndex 化される。
            //    Prefab 内の親子関係も外部 ID を残さず、自己完結したファイルになる。
            meta->serialize(compJson, data, &saveCtx);
            entityJson[CCL::Serialization::Keys::Components][cName] = std::move(compJson);
            ++savedComponentCount;
        }
        root[CCL::Serialization::Keys::Entities].push_back(std::move(entityJson));
    }

    // ---- 5. ファイル書き出し ----
    std::string jsonStr = root.dump(4);
    if (!FileSystem::WriteAllText(filepath, jsonStr)) {
        CCL_LOG_ERROR(LogCategory::Editor, "[Prefab] %s の書き出しに失敗しました", filepath.c_str());
        return false;
    }

    // 何がファイルに入ったかを必ず1行残す。「保存したのに中身が違う」ときに
    //   選択エンティティを間違えたのかどうかを、ここだけ見て切り分けられる。
    CCL_LOG_INFO(LogCategory::Editor,
        "[Prefab] %s を保存 (エンティティ %zu 個 / コンポーネント %zu 個)",
        filepath.c_str(), entitiesToSave.size(), savedComponentCount);

    return true;
}