// =====================================================================
// SceneSerializer.h
//   シーン (= ワールド全体のスナップショット) の JSON ⇔ メモリ変換。
//
//   旧版との主な違い:
//     1. EntityID は raw uint64_t ではなく「ローカルインデックス」で書き出す
//     2. 親子・リンクのリマップはハードコード分岐ではなく、
//        FieldKind::EntityLink による自動処理
//     3. ヘッダにバージョン情報を持たせる (将来の互換性のため)
//
//   依存:
//     <json_fwd.hpp> 経由で前方宣言のみ。本体は .cpp で include。
// =====================================================================
#pragma once
#include "Engine/ECS/Core/CCL_World.h"
#include <string>
#include <vector>
#include <json_fwd.hpp>

namespace CCL::Serialization { class SaveContext; class LoadContext; }

class SceneSerializer {
public:
    // ---- 保存 ----
    // ファイルに書き出す。失敗時 false を返す。
    static bool Serialize(CCL::ECS::World* world, const std::string& filepath);

    // ---- 読み込み ----
    // 結果として生成された EntityID リストを返す (Prefab 化したい時など便利)。
    // 失敗時は空 vector。
    static std::vector<CCL::ECS::EntityID> Deserialize(
        CCL::ECS::World* world, const std::string& filepath);

    // JSON オブジェクトから直接ロード (Prefab 内で使う / 通信ペイロードから等)。
    // entitiesJson は配列を想定 (root["entities"] と同等)。
    static std::vector<CCL::ECS::EntityID> DeserializeRaw(
        CCL::ECS::World* world, const nlohmann::json& entitiesJson);

private:
    // 1 エンティティ分の書き出し
    static void SerializeEntity(
        CCL::ECS::World* world,
        CCL::ECS::EntityID entity,
        nlohmann::json& outJson,
        CCL::Serialization::SaveContext& saveCtx);
};