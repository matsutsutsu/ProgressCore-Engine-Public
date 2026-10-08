#pragma once
// ===========================================================================
// 概要: World の Entity を JSON ファイルへ保存・読込するクラス SceneSerializer。
//
// 責務:
//   - World 全体をシーンファイルに保存する（削除予定・常駐・UI・プレハブの Entity は除く）
//   - シーン・プレハブのファイルから Entity を生成する
//   - Entity とその子孫をメモリ上で複製する（エディタ用）
//
// 備考:
//   - EntityID はローカルインデックスに変換して書き、読込時に新しい EntityID へ戻す（EntityLink）。
//   - このヘッダは json_fwd.hpp の前方宣言だけを使い、json.hpp は .cpp で読む。
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include <string>
#include <vector>
#include <json_fwd.hpp>

namespace CCL::Serialization
{
    class SaveContext;
    class LoadContext;
} // namespace CCL::Serialization

class SceneSerializer
{
public:
    // World をファイルに保存する。失敗したら false
    static bool Serialize(CCL::ECS::World *world, const std::string &filepath);

    // ファイルを読んで Entity を生成し、生成した EntityID を返す。失敗したら空。
    //   毎回ディスクを読んでパースする。同じファイルを繰り返し生成するなら Prefab::SpawnPrefab を使う
    static std::vector<CCL::ECS::EntityID> Deserialize(
        CCL::ECS::World *world, const std::string &filepath);

    // entities 配列（ファイルの root["entities"]）から Entity を生成する
    static std::vector<CCL::ECS::EntityID> DeserializeRaw(
        CCL::ECS::World *world, const nlohmann::json &entitiesJson);

    /**
     * @brief ファイルを読み、magic / version を検証して entities 配列を取り出す。
     *
     * Prefab がパース結果をキャッシュできるよう、読込から切り出して公開している。
     *
     * @param filepath    読み込むファイル。
     * @param outEntities entities 配列を受け取る。失敗したときは変えない。
     * @return 読めたら true。
     */
    static bool LoadEntitiesJson(const std::string &filepath, nlohmann::json &outEntities);

    // Entity とその子孫を複製し、生成した EntityID を返す（[0] が複製のルート）。失敗したら空。
    //   保存と読込をメモリ上で行うので、保存対象のコンポーネントは全てコピーされ、
    //   子孫の中の親子・EntityLink は複製同士で張り直される。複製のルートは元と同じ親の子になる
    static std::vector<CCL::ECS::EntityID> DuplicateHierarchy(
        CCL::ECS::World *world, CCL::ECS::EntityID root);

private:
    // Entity 1 つ分を JSON に書く
    static void SerializeEntity(
        CCL::ECS::World                 *world,
        CCL::ECS::EntityID               entity,
        nlohmann::json                  &outJson,
        CCL::Serialization::SaveContext &saveCtx);
};