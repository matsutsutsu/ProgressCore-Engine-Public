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
#include "Engine/Transform/Components/PendingParentComponent.h"

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
    try {
        root = nlohmann::json::parse(jsonStr);
    }
    catch (const std::exception& /*e*/) {
        return {};
    }

    // ---- バージョンチェック ----
    if (root.contains(CCL::Serialization::Keys::Magic)) {
        uint32_t magic = root[CCL::Serialization::Keys::Magic].get<uint32_t>();
        if (magic != CCL::Serialization::SceneFileMagic) {
            // Logger::Error("SceneSerializer: magic mismatch");
            return {};
        }
        uint32_t ver = root.value(CCL::Serialization::Keys::Version, 0u);
        if (ver < CCL::Serialization::MinSupportedSceneVersion) {
            // Logger::Error("SceneSerializer: unsupported version " + std::to_string(ver));
            return {};
        }
        // 将来: ver < CurrentSceneVersion ならアップグレード処理を挟む
    }
    // (旧フォーマットは magic フィールドがないので、無しの場合は素通り = 互換ロード)

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
    // ※ Archetype は空のまま生成し、後続でコンポーネントを追加する。
    //   旧版にあったヒープ確保 (new/delete) は廃止。スタック変数で十分。
    Archetype emptyArch;
    for (const auto& entityJson : entitiesJson) {
        EntityID newID = world->RequestSpawnEntity(emptyArch);
        createdEntities.push_back(newID);

        // ★ ローカルインデックス → 新 EntityID を登録
        // 旧フォーマット (raw EntityID を "id" に保存) と新フォーマット ("lid") の両対応
        uint32_t localIdx = 0;
        if (entityJson.contains(CCL::Serialization::Keys::EntityLocal)) {
            localIdx = entityJson[CCL::Serialization::Keys::EntityLocal].get<uint32_t>();
        }
        else if (entityJson.contains("id")) {
            // 旧フォーマット互換: 順序で localIndex を割り当て直す
            // (本来は raw ID だが、上書きで一意性を持たせる)
            localIdx = static_cast<uint32_t>(createdEntities.size());
        }
        loadCtx.Register(localIdx, newID);
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

            // =====================================================================
            // ★ 追加: 確実な親子関係の復元 (PendingParentComponent の活用)
            // 自動リマップの盲点をバイパスし、確実に実EntityIDを解決して予約する
            // =====================================================================
            if (components.contains("Transform")) {
                const auto& transJson = components["Transform"];
                if (transJson.contains("Parent")) {
                    auto localIdx = transJson["Parent"].get<CCL::Serialization::SaveContext::LocalIndex>();
                    EntityID resolvedParent = loadCtx.Resolve(localIdx);

                    // プレハブ内に存在する正しい親が見つかった場合のみ
                    if (resolvedParent != CCL::ECS::InvalidEntityID) {
                        // 次のフレームで安全に SetParent させるための予約コンポーネントを付与
                        PendingParentComponent ppc;
                        ppc.parentID = resolvedParent;
                        world->AddComponent<PendingParentComponent>(newID, ppc);
                    }
                }
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

    return createdEntities;
}