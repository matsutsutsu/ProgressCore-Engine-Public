// =====================================================================
// SceneSerializer.cpp
//   ★ 旧版の致命的問題を解消した実装。
//
//   解消した点:
//     [旧] "if (components.contains("Transform"))" のようなハードコード分岐
//     → [新] FieldKind::EntityLink を持つフィールドは自動で remap される。
//            新規コンポーネントを足してもこのファイルは触らない。
//
//     [旧] raw EntityID をそのまま保存
//     → [新] ローカルインデックス (1, 2, 3, ...) で保存。マージ・移植可能。
//
//     [旧] new/delete で Archetype を毎エンティティ確保
//     → [新] スタック変数で済ます (RequestSpawnEntity が値を取るため)。
//
//   復元の Pass 構成 (重要):
//     Pass 1: 全エンティティを Spawn し、LoadContext に localIndex を登録
//             この時点ではコンポーネントは付いていない
//     Pass 2: ScrutinyAndApply でエンティティ実体化 → コンポーネント追加 (deserialize)
//             この段階で EntityLink フィールドは LoadContext.Resolve() で
//             自動的に新 EntityID に変換される
//     Pass 3: ScrutinyAndApply で構造変更を確定
// =====================================================================
#include "SceneSerializer.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Core/SaveContext.h"
#include "Engine/Serialization/Core/LoadContext.h"
#include "Engine/Serialization/Core/SceneFormat.h"

#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Core/Components/PersistentTag.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"

#include <json.hpp>
#include <fstream>
#include <iomanip>
#include <chrono>

using namespace CCL::ECS;
using namespace CCL::ECS::Core;
using CCL::Serialization::SaveContext;
using CCL::Serialization::LoadContext;
using CCL::Serialization::EntityLink;

// =====================================================================
// 保存 (Serialize)
// =====================================================================
bool SceneSerializer::Serialize(World* world, const std::string& filepath)
{
    if (!world) return false;

    nlohmann::json root;

    // ---- ヘッダ ----
    // ファイル先頭で「これは何のフォーマットか/どのバージョンか」を即判定できるようにする
    root[CCL::Serialization::Keys::Magic] = CCL::Serialization::SceneFileMagic;
    root[CCL::Serialization::Keys::Version] = CCL::Serialization::CurrentSceneVersion;
    root[CCL::Serialization::Keys::Timestamp] =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    // ---- 第1段階: 保存対象エンティティを全て収集し、ローカルインデックスを発行 ----
    // EntityLink の参照解決のため、本体書き出し前に全エンティティを登録しておく必要がある。
    // (例: A が B を参照していて、B が後ろにいる場合でも B の localIndex が確定している)
    SaveContext saveCtx;
    std::vector<EntityID> targets;

    {
        auto& chunkManager = world->GetChunkManager();
        for (const auto& upChunk : chunkManager.GetChunks()) {
            if (!upChunk) continue;
            Chunk* chunk = upChunk.get();

            // 保存対象外の判定
            if (chunk->HasComponent<Tag::DestroyTag>())    continue; // 削除予定
            if (chunk->HasComponent<Tag::PersistentTag>()) continue; // シーン跨ぎの常駐物

            size_t          count = chunk->GetEntityCount();
            const EntityID* ids = chunk->GetEntityIDs();
            for (size_t i = 0; i < count; ++i) {
                if (chunk->IsEntityDestroyed(i)) continue;
                EntityID id = ids[i];
                saveCtx.GetOrRegister(id);  // ★ ここで localIndex 発行
                targets.push_back(id);
            }
        }
    }

    // ---- 第2段階: エンティティの本体を書き出し ----
    nlohmann::json entitiesArray = nlohmann::json::array();
    for (EntityID id : targets) {
        nlohmann::json entityJson;
        SerializeEntity(world, id, entityJson, saveCtx);
        entitiesArray.push_back(std::move(entityJson));
    }
    root[CCL::Serialization::Keys::Entities] = std::move(entitiesArray);

    // ---- 第3段階: ファイル書き出し ----
    std::string jsonStr = root.dump(4); // インデント4で文字列化
    return FileSystem::WriteAllText(filepath, jsonStr);
}

void SceneSerializer::SerializeEntity(
    World* world, EntityID entityID, nlohmann::json& outJson, SaveContext& saveCtx)
{
    // ★ raw EntityID ではなく、ローカルインデックスで書き出す
    outJson[CCL::Serialization::Keys::EntityLocal] = saveCtx.GetOrRegister(entityID);
    outJson[CCL::Serialization::Keys::Components] = nlohmann::json::object();

    auto& registry = ComponentRegistry::Instance();

    // エンティティが現在所属する Chunk を引き、Archetype を巡回
    size_t index = 0;
    size_t chunkIdx = world->GetChunkManager().SearchEntityIn(entityID, &index);
    if (chunkIdx == InvalidIndex) return;

    Chunk* chunk = world->GetChunkManager().GetChunks()[chunkIdx].get();
    const auto& archetype = chunk->GetArchetype();

    for (const auto& typeData : archetype) {
        std::string name = registry.GetNameByTypeID(typeData.id);
        if (name.empty()) continue;

        void* data = chunk->GetComponentPtrByType(typeData.id, index);
        const auto* meta = registry.GetInfoByName(name);
        if (!meta || !data) continue;
        if (!meta->isSerializable) continue;   // ★ 保存対象外はスキップ

        nlohmann::json compJson;
        // ★ SaveContext を渡して、EntityLink フィールドが自動で localIndex に変換される
        meta->serialize(compJson, data, &saveCtx);
        outJson[CCL::Serialization::Keys::Components][name] = std::move(compJson);
    }
}

// =====================================================================
// 読み込み (Deserialize)
// =====================================================================
std::vector<EntityID> SceneSerializer::Deserialize(World* world, const std::string& filepath)
{
    // ★ 変更後:
    std::string jsonStr = FileSystem::ReadAllText(filepath);
    if (jsonStr.empty()) {
        return {};
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(jsonStr);
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::Core,
            "[SceneSerializer] JSON parse failed (%s): %s", filepath.c_str(), e.what());
        return {};
    }

    // ---- バージョンチェック ----
    // ---- フォーマット検証 (新フォーマットのみ: magic 必須) ----
    if (!root.contains(CCL::Serialization::Keys::Magic))
    {
        CCL_LOG_ERROR(LogCategory::Core,
            "[SceneSerializer] missing magic — not a valid scene/prefab file: %s", filepath.c_str());
        return {};
    }
    {
        uint32_t magic = root[CCL::Serialization::Keys::Magic].get<uint32_t>();
        if (magic != CCL::Serialization::SceneFileMagic)
        {
            CCL_LOG_ERROR(LogCategory::Core,
                "[SceneSerializer] magic mismatch: %s", filepath.c_str());
            return {};
        }
        uint32_t ver = root.value(CCL::Serialization::Keys::Version, 0u);
        if (ver < CCL::Serialization::MinSupportedSceneVersion)
        {
            CCL_LOG_ERROR(LogCategory::Core,
                "[SceneSerializer] unsupported version %u: %s", ver, filepath.c_str());
            return {};
        }
    }

    if (!root.contains(CCL::Serialization::Keys::Entities)) return {};
    return DeserializeRaw(world, root[CCL::Serialization::Keys::Entities]);
}

std::vector<EntityID> SceneSerializer::DeserializeRaw(World* world, const nlohmann::json& entitiesJson)
{
    if (!world || !entitiesJson.is_array()) return {};

    std::vector<EntityID> createdEntities;
    createdEntities.reserve(entitiesJson.size());

    LoadContext loadCtx;
    loadCtx.Reserve(entitiesJson.size());

    auto& registry = ComponentRegistry::Instance();

    // ---------------------------------------------------------
    // Pass 1: エンティティ一括 Spawn + LoadContext へ登録
    // ---------------------------------------------------------
    Archetype emptyArch;
    for (const auto &entityJson : entitiesJson)
    {
        EntityID newID = world->RequestSpawnEntity(emptyArch);
        createdEntities.push_back(newID);

        // 新フォーマットのみ: ローカルインデックス (lid) → 新 EntityID を登録
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

    // ---------------------------------------------------------
    // Pass 2: コンポーネント復元 (PendingOp に積まれる)
    // ---------------------------------------------------------
    {
        size_t i = 0;
        for (const auto& entityJson : entitiesJson) {
            EntityID newID = createdEntities[i++];
            if (!entityJson.contains(CCL::Serialization::Keys::Components)) continue;

            const auto& components = entityJson[CCL::Serialization::Keys::Components];
            for (auto it = components.begin(); it != components.end(); ++it) {
                const std::string& compName = it.key();
                const auto& compJson = it.value();
                const auto* meta = registry.GetInfoByName(compName);
                if (!meta) continue;
                // 通常のデシリアライズ（ここでゴミの parentID が入る可能性がある）
                meta->deserializeToWorld(world, newID, compJson, &loadCtx);
            }
        }
    }

    // ---------------------------------------------------------
    // Pass 3: ScrutinyAndApply で構造変更を確定
    //   - PendingOp に積まれた AddComponent が物理的に反映される
    //   - Pass 2 で remap 済みの EntityLink は既に正しい値が入っている
    //   - PendingParentComponent 等は ScrutinyAndApply で処理されるはず
    // ---------------------------------------------------------
    world->ScrutinyAndApply();

    // ---------------------------------------------------------
    // Pass 4: 親子リンクの最終確定 (文字列に依存しない)
    //   EntityLink により各 Transform の parent.id は既に実 EntityID へ解決済み。
    //   ただし保存しない firstChild/兄弟リンク(下方向)は未構築なので、
    //   解決済み parent.id を使って SetParent で双方向リンクを張り直す。
    //   createdEntities は root→子の順(BFS)なので親が先に処理される。
    // ---------------------------------------------------------
    for (EntityID id : createdEntities)
    {
        auto *t = world->GetComponent<TransformComponent>(id);
        if (!t)
            continue;
        if (!t->parent.IsValid())
            continue; // ルート/親なしは対象外
        TransformUpdateSystem::SetParent(*world, id, t->parent.id);
    }


    return createdEntities;
}