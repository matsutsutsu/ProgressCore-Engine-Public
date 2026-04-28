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

std::vector<EntityID> SceneSerializer::DeserializeRaw(World *world, const json &entitiesJson)
{
    std::vector<EntityID> createdEntities;

    // ---------------------------------------------------
    // Pass 1: エンティティ生成 & ID登録
    // ---------------------------------------------------
    for (const auto &entityJson : entitiesJson) {
        // 空のエンティティを生成
        Archetype emptyArch;
        EntityID  newID = world->RequestSpawnEntity(emptyArch);
        createdEntities.push_back(newID);

        // ★修正点2: "id" がない場合(旧形式)でもクラッシュさせない
        if (entityJson.contains("id")) {
            uint64_t oldID = entityJson["id"].get<uint64_t>();
            SerializationContext::RegisterID(oldID, newID);
        }
    }

    // ---------------------------------------------------
    // Pass 2: コンポーネント復元 & 親子関係リンク
    // ---------------------------------------------------
    auto &registry = ComponentRegistry::Instance();

 // インデックスを使ってループ (createdEntitiesと同期させるため)
    size_t index = 0;
    for (const auto &entityJson : entitiesJson) {
        EntityID newID = createdEntities[index];
        index++;

        if (entityJson.contains("components")) {
            auto &components = entityJson["components"];
            for (auto it = components.begin(); it != components.end(); ++it) {
                std::string name = it.key();
                auto       &data = it.value();

                const auto *meta = registry.GetInfoByName(name);
                if (meta) {
                    // 内部で SerializationContext::ResolveID が呼ばれた際、
                    // 旧形式でIDがなくても、TransformのparentIDが0なら問題なく動作します
                    meta->deserializeToWorld(world, newID, data);
                }
            }
        }
    }

    // =========================================================================
      //  Pass 3 親子関係の完全な再構築（TransformUpdateSystemへの委譲）
      // =========================================================================
    for (EntityID newID : createdEntities) {
        auto* trans = world->GetComponent<TransformComponent>(newID);
        if (trans) {
            // Pass 2 で、SerializationContext によって既に「新しいID」に
            // 翻訳された親IDが trans->parentID に入っている。
            EntityID mappedParentID = trans->parentID;

            // ① 親が存在する場合のみ、予約票 (PendingParent) を発行する
            if (mappedParentID != 0 && mappedParentID != CCL::ECS::InvalidEntityID) {
                if (world->HasComponent<PendingParentComponent>(newID)) {
                    world->GetComponent<PendingParentComponent>(newID)->parentID = mappedParentID;
                }
                else {
                    PendingParentComponent pendingParent;
                    pendingParent.parentID = mappedParentID;
                    world->AddComponent<PendingParentComponent>(newID, pendingParent);
                }
            }

            // ② ★超重要★ リンクの初期化 (白紙に戻す)
            // 不完全なポインタが残っていると TransformUpdateSystem がクラッシュするため、
            // 予約票だけを持たせて、物理的なリンクはすべて断ち切った状態で生成を終える。
            trans->parentID = 0;
            trans->firstChildID = 0;
            trans->nextSiblingID = 0;
            trans->prevSiblingID = 0;
        }
    }

    // 生成を確定
    //world->ScrutinyAndApply();

    return createdEntities;
}