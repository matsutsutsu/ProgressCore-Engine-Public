// Editor/Serialization/SceneSerializer.cpp
#include "SceneSerializer.h"
#include "ComponentRegistry.h"
#include "SerializationContext.h"
#include <fstream>
#include <iomanip>

#include "Engine/GamePlay/Transform/PendingParentComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"

#include "Engine/GamePlay/Core/DestroyTag.h"
#include "Engine/GamePlay/Core/PersistentTag.h"

#include "Engine/Platform/Logger.h"

using namespace CCL::ECS;
using namespace CCL::ECS::Core;

// -----------------------------------------------------------------------
// 保存 (Serialize)
// -----------------------------------------------------------------------
void SceneSerializer::Serialize(World *world, const std::string &filepath)
{
    nlohmann::json root;
    root["entities"] = nlohmann::json::array();

    auto &chunkManager = world->GetChunkManager();
    auto &chunks       = chunkManager.GetChunks();

    // 全エンティティを走査して保存
    for (const auto &upChunk : chunks) {
        if (!upChunk) continue;
        Chunk          *chunk     = upChunk.get();

        // =========================================================
        // ★ アーキテクトの極意：セーブデータの汚染を物理的に防ぐ
        // =========================================================
        // 1. 削除予定の「ゴミ」は保存しない
        if (chunk->HasComponent<Tag::DestroyTag>()) continue;

        // 2. シーンを跨いで常駐する「システム（BGMやManager等）」は保存しない
        // （※これらはエンジン起動時に生成されるため、シーンに保存すると二重生成される）
        if (chunk->HasComponent<Tag::PersistentTag>()) continue;

        size_t          count     = chunk->GetEntityCount();
        const EntityID *entityIDs = chunk->GetEntityIDs();

        for (size_t i = 0; i < count; ++i) {
            if (chunk->IsEntityDestroyed(i)) continue;

            json entityJson;
            SerializeEntity(world, entityIDs[i], entityJson);
            root["entities"].push_back(entityJson);
        }
    }

    std::ofstream o(filepath);
    o << std::setw(4) << root << std::endl;
}

void SceneSerializer::SerializeEntity(World *world, EntityID entityID, json &outJson)
{
    outJson["id"]         = (uint64_t)entityID; // IDを保存
    outJson["components"] = json::object();

    auto &registry = ComponentRegistry::Instance();

    // Entityが持っているコンポーネントを検索
    // (ここではWorld->GetComponentを使わず、Chunkから取るのが高速だが、
    //  汎用性のためComponentRegistry経由の実装イメージとします)

    // 実際の実装では、EntityのArchetypeを取得してループするのが正解です。
    // 簡易的に全登録コンポーネントをチェックする、またはChunkから取得するロジックにします。

    // Chunk経由の取得ロジック（推奨）:
    size_t index    = 0;
    size_t chunkIdx = world->GetChunkManager().SearchEntityIn(entityID, &index);
    if (chunkIdx != InvalidIndex) {
        Chunk      *chunk     = world->GetChunkManager().GetChunks()[chunkIdx].get();
        const auto &archetype = chunk->GetArchetype();

        for (const auto &typeData : archetype) {
            std::string name = registry.GetNameByTypeID(typeData.id);
            if (name.empty()) continue;

            void       *data = chunk->GetComponentPtrByType(typeData.id, index);
            const auto *meta = registry.GetInfoByName(name);
            if (meta && data) {
                json compJson;
                meta->serialize(compJson, data);
                outJson["components"][name] = compJson;
            }
        }
    }
}

// -----------------------------------------------------------------------
// 読み込み (Deserialize)
// -----------------------------------------------------------------------
std::vector<EntityID> SceneSerializer::Deserialize(World *world, const std::string &filepath)
{
    std::ifstream i(filepath);
    if (!i.is_open()) return {};

    nlohmann::json root;
    try {
        i >> root;
    }
    catch (...) {
        return {};
    }

    // 以前のIDマップをクリア
    // (Prefabロードなどの場合、呼び出し元で制御が必要かもだが、基本クリアでOK)
    SerializationContext::Clear();

   // ★修正点1: フォーマット判別
    if (root.contains("entities")) {
        // 新しい形式 (シーン保存したもの)
        return DeserializeRaw(world, root["entities"]);
    }
    else if (root.contains("components")) {
        // ★旧形式 (単体Prefab) への対応
        // これを 1要素だけの配列 として扱う
        nlohmann::json wrapper = nlohmann::json::array();
        wrapper.push_back(root);
        return DeserializeRaw(world, wrapper);
    }

    // キーが見つからなかった場合（BOM等によるエラー時）に備え、空配列を返す
    return {};
}

std::vector<EntityID> SceneSerializer::DeserializeRaw(World* world, const json& entitiesJson)
{
    std::vector<EntityID> createdEntities;

    // ★ 究極の修正1: このPrefabロード1回分「専用」の翻訳辞書を作る
    std::unordered_map<uint64_t, EntityID> localIdMap;

    auto& registry = ComponentRegistry::Instance();

    // ---------------------------------------------------
    // Pass 1: エンティティの一括生成 (Batch Spawn) & ID登録
    // ---------------------------------------------------
    for (const auto& entityJson : entitiesJson) {

        // ★ 究極の修正4: JSONから必要なコンポーネントの型情報を先に集め、
        // アーキタイプを事前に完成させる
        Archetype fullArch;

        if (entityJson.contains("components")) {
            const auto& components = entityJson["components"];
            for (auto it = components.begin(); it != components.end(); ++it) {
                std::string compName = it.key();

                // レジストリから型情報(TypeData)を取得してアーキタイプに追加
                // （※エンジン仕様に合わせ、RegistryからTypeIDを引く関数を使用）
                const auto* meta = registry.GetInfoByName(compName);
                if (meta) {
                    // TypeID を名前から引く (ComponentRegistryに機能がある前提)
                    // ない場合はメタのdeserializeToWorld内でAddComponentされるのを待つため、
                    // ここでは一旦 emptyArch で生成し、Pass2で追加させるフォールバックも可能。
                    // 今回はエンジン内部に依存しすぎないよう、一旦emptyArchで生成する
                    // （※「一気にSpawn」の完全実装には ComponentRegistry::GetTypeData(name) が必要です）
                }
            }
        }

        // 一旦、安全地帯(ヒープ)に空のアーキタイプを作り、Spawnを予約する
        Archetype* heapArch = new Archetype();
        EntityID newID = world->RequestSpawnEntity(*heapArch);

        // ★ 修正: RequestSpawnEntity内でヒープにコピーされたので、ここで作ったheapArchは即消す
        delete heapArch;

        createdEntities.push_back(newID);

        // ★ 究極の修正2: 専用辞書に登録する
        if (entityJson.contains("id")) {
            uint64_t oldID = entityJson["id"].get<uint64_t>();
            localIdMap[oldID] = newID;
        }
    } // ← ★ ここ！ Pass 1の forループの閉じカッコが抜けていました！

    // ---------------------------------------------------
    // Pass 2: コンポーネント復元 & 親子関係リンク
    // ---------------------------------------------------
    // インデックスを使ってループ (createdEntitiesと同期させるため)
    size_t index = 0;
    for (const auto& entityJson : entitiesJson) {
        EntityID newID = createdEntities[index++];

        if (entityJson.contains("components")) {
            auto& components = entityJson["components"];
            for (auto it = components.begin(); it != components.end(); ++it) {
                std::string name = it.key();
                auto& data = it.value();

                const auto* meta = registry.GetInfoByName(name);
                if (meta) {
                    // ★ コンポーネントの中身を復元し、PendingOpに積む
                    // (ここで BoneAttachmentComponent などが実体化される)
                    meta->deserializeToWorld(world, newID, data);
                }
            }
        }
    }

    // ---------------------------------------------------
    // Pass 3: 親子関係の完全な再構築
    // ---------------------------------------------------
    // ★ 究極の修正3: ローカル辞書を使って翻訳する

    size_t fixIndex = 0;
    for (const auto& entityJson : entitiesJson)
    {
        EntityID newID = createdEntities[fixIndex++];

        if (!entityJson.contains("components")) continue;
        const auto& components = entityJson["components"];
        if (!components.contains("Transform")) continue;

        const auto& transformJson = components["Transform"];

        // 旧キー "Parent Entity ID" または新キー "parentID" を取得
        uint64_t oldParentID = 0;
        if (transformJson.contains("parentID")) {
            oldParentID = transformJson["parentID"].get<uint64_t>();
        }
        else if (transformJson.contains("Parent Entity ID")) {
            oldParentID = transformJson["Parent Entity ID"].get<uint64_t>();
        }

        // 親が存在する場合
        if (oldParentID != 0) {
            EntityID resolvedParentID = 0;

            // ★ グローバル辞書(SerializationContext)ではなく、ローカル辞書から引く！
            if (localIdMap.find(oldParentID) != localIdMap.end()) {
                resolvedParentID = localIdMap[oldParentID];
            }

            // 翻訳に成功したら、予約票を発行
            if (resolvedParentID != 0 && resolvedParentID != CCL::ECS::InvalidEntityID) {
                if (world->HasComponent<PendingParentComponent>(newID)) {
                    world->GetComponent<PendingParentComponent>(newID)->parentID = resolvedParentID;
                }
                else {
                    PendingParentComponent pendingParent;
                    pendingParent.parentID = resolvedParentID;
                    world->AddComponent<PendingParentComponent>(newID, pendingParent);
                }
            }
        }
    }

    return createdEntities;
}