#include "LevelLoader.h"
#include "Engine/Serialization/Factory/Prefab.h" // 壁や松明のPrefab生成用
#include "StageMap.h"
#include <fstream>
#include <SimpleMath.h>
#include <iostream> // デバッグ出力用
#include "Systems/MeshBatcher.h" // 作成したBatcher
#include "Engine/Graphics/Resource/ResourceManager.h" // リソース管理

#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/GamePlay/Graphics/Core/MaterialComponent.h"
#include "Engine/GamePlay/Graphics/Core/StaticMeshComponent.h"
#include "Engine/GamePlay/Physics/Collision/BoxColliderComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"

#include "Game/Logics/Combat/HealthComponent.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/Combat/Weapon/Core/GunComponent.h"

#include "Game/Logics/Character/Enemy/EnemyComponent.h"

#include "Engine/GamePlay/Camera/VirtualCameraComponents.h"

#include "Engine/GamePlay/Core/Scene/SceneManager.h"

#include "Generation/MapGenerator.h"
#include "Engine/Serialization/Factory/Prefab.h"

#include "Generation/RoomTemplateManager.h"

// ===================================================================================
// ファイル名: LevelLoader.cpp
//
// 【役割・責任】
//   データ(StageMap) を ゲームの実体(Entity) に変換する処理を行います。
//
// 【主な処理フロー】
//   1. Generator でマップデータを作る (tiles配列ができる)
//   2. MeshBatcher で描画用の結合モデルを作る (見た目ができる)
//   3. Prefab::Spawn でプレイヤーや敵を配置する (キャラクターが出る)
//   4. StageMap を Worldのリソースとして登録する (他のシステムがマップ情報を参照できるようになる)
// ===================================================================================

using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace CCL::ECS;


void LevelLoader::Initialize()
{
    // 初期化フェーズにて
    auto &tm = RoomTemplateManager::Instance();
    tm.Clear();

    // --- ステージ1用 ---
    // ファイルパスを直接指定して登録
    tm.RegisterTemplate(1, "Assets/GamePlay/RoomTemplates/Room_Cross_15.json");
    tm.RegisterTemplate(1, "Assets/GamePlay/RoomTemplates/Room_Eye_15.json");
    tm.RegisterTemplate(1, "Assets/GamePlay/RoomTemplates/Room_FiveDice_20.json");
    tm.RegisterTemplate(1, "Assets/GamePlay/RoomTemplates/Room_FourPillar_20.json");
    tm.RegisterTemplate(1, "Assets/GamePlay/RoomTemplates/Room_Pillar_15.json");
    tm.RegisterTemplate(1, "Assets/GamePlay/RoomTemplates/Room_SixDice_20.json");
    tm.RegisterTemplate(1, "Assets/GamePlay/RoomTemplates/Room_Square_20.json");

    // --- ステージ2用 ---

    for (int i = 1; i < 5; ++i) {

        tm.RegisterTemplate(i, "Assets/GamePlay/RoomTemplates/Room_Cross_15.json");
        tm.RegisterTemplate(i, "Assets/GamePlay/RoomTemplates/Room_Eye_15.json");
        tm.RegisterTemplate(i, "Assets/GamePlay/RoomTemplates/Room_FiveDice_20.json");
        tm.RegisterTemplate(i, "Assets/GamePlay/RoomTemplates/Room_FourPillar_20.json");
        tm.RegisterTemplate(i, "Assets/GamePlay/RoomTemplates/Room_Pillar_15.json");
        tm.RegisterTemplate(i, "Assets/GamePlay/RoomTemplates/Room_SixDice_20.json");
        tm.RegisterTemplate(i, "Assets/GamePlay/RoomTemplates/Room_Square_20.json");
    }
}


// ===================================================================================
// 共通処理: StageMapデータから描画用メッシュを構築する
// ===================================================================================
void LevelLoader::BuildStageMesh(
    CCL::ECS::Core::World *world, ID3D11Device *device, const StageMap &map)
{
    // モデルパレットの準備
    std::unordered_map<TileType, std::shared_ptr<Model>> palette;

    // ※パスはプロジェクトに合わせて確認してください
    palette[TileType::Wall]  = std::make_shared<Model>(device, "Assets/Models/MapChip/Wall.glb");
    palette[TileType::Floor] = std::make_shared<Model>(device, "Assets/Models/MapChip/NewModel/Yuka.glb");

    // チャンクサイズ（例: 10x10マスごとに1つのモデルにする）
    const int ChunkSize = 20;

    for (int cz = 0; cz < map.height; cz += ChunkSize) {
        for (int cx = 0; cx < map.width; cx += ChunkSize) {

            // --- A. 壁の結合バッチ ---
            {
                std::unordered_map<TileType, std::shared_ptr<Model>> wallPalette;
                wallPalette[TileType::Wall] = palette[TileType::Wall];

                auto batchData =
                    MeshBatcher::BuildChunk(map, cx, cz, ChunkSize, ChunkSize, wallPalette);

                if (!batchData.vertices.empty()) {
                    auto baseMat    = palette[TileType::Wall]->GetMaterials()[0].data;
                    auto chunkModel = Model::CreateFromData(
                        device, batchData.vertices, batchData.indices, baseMat);

                    auto arch   = CCL::ECS::ArchetypeHelper::Generate<TransformComponent,
                          ModelComponent,
                          MaterialComponent,
                          StageMapChunkComponent>();
                    auto entity = world->RequestSpawnEntity(arch);

                    world->AddComponent<ModelComponent>(entity, ModelComponent(chunkModel));
                    world->AddComponent<MaterialComponent>(entity, MaterialComponent{});
                   
                    TransformComponent trans;
                    trans.position = {0, 0, 0};

                    // ★最適化設定
                    trans.isStatic = true; // 今後更新しない
                    trans.UpdateMatrix();  // 最初の一回だけ計算して worldMatrix に値を焼き付ける！

                    world->AddComponent<TransformComponent>(entity, std::move(trans));
                }
            }

            // --- B. 床の結合バッチ ---
            {
                std::unordered_map<TileType, std::shared_ptr<Model>> floorPalette;
                floorPalette[TileType::Floor] = palette[TileType::Floor];

                auto batchData =
                    MeshBatcher::BuildChunk(map, cx, cz, ChunkSize, ChunkSize, floorPalette);

                if (!batchData.vertices.empty()) {
                    auto baseMat    = palette[TileType::Floor]->GetMaterials()[0].data;
                    auto chunkModel = Model::CreateFromData(
                        device, batchData.vertices, batchData.indices, baseMat);

                    auto arch   = CCL::ECS::ArchetypeHelper::Generate<TransformComponent,
                          ModelComponent,
                          MaterialComponent,
                          StageMapChunkComponent>();
                    auto entity = world->RequestSpawnEntity(arch);

                    world->AddComponent<ModelComponent>(entity, ModelComponent(chunkModel));
                    world->AddComponent<MaterialComponent>(entity, MaterialComponent{});
                    
                    TransformComponent trans;
                    trans.position = {0, 0, 0};
                    // ★最適化設定
                    trans.isStatic = true; // 今後更新しない
                    trans.UpdateMatrix();  // 最初の一回だけ計算して worldMatrix に値を焼き付ける！
                    world->AddComponent<TransformComponent>(entity, std::move(trans));
                }
            }
        }
    }
}


// ------------------------------------------------------------------
// ★追加: プレイヤーデータの保存 (GateSystem等から呼ぶ)
// ------------------------------------------------------------------
void LevelLoader::SavePlayerData(CCL::ECS::Core::World *world)
{
    auto view = world->View<PlayerComponent>();
    if (view.empty()) return;

    EntityID playerID = view[0];
    auto    &manager  = SceneManager::Instance().GetPlayerData();

    // HP保存
    if (auto *hp = world->GetComponent<HealthComponent>(playerID)) {
        manager.currentHp = hp->currentHealth;
        manager.maxHp     = hp->maxHealth;
    }
    // 銃データ保存
    if (auto *gun = world->GetComponent<GunComponent>(playerID)) {
        manager.gunConfig = gun->gun;
    }

    manager.hasData = true;
    std::cout << "[LevelLoader] Player data saved." << std::endl;
}

// ------------------------------------------------------------------
// ★追加: プレイヤーデータの復元 (Load関数内から呼ぶ)
// ------------------------------------------------------------------
void LevelLoader::RestorePlayerData(CCL::ECS::Core::World *world, CCL::ECS::EntityID playerID)
{
    auto &manager = SceneManager::Instance().GetPlayerData();
    if (!manager.hasData) return;

    // HPの復元
    if (auto *hp = world->GetComponent<HealthComponent>(playerID)) {
        hp->currentHealth = manager.currentHp;
        hp->maxHealth     = manager.maxHp;
    }
    // 銃の復元
    if (auto *gun = world->GetComponent<GunComponent>(playerID)) {
        gun->gun = manager.gunConfig;
    }
    std::cout << "[LevelLoader] Player data restored. HP: " << manager.currentHp << std::endl;
}

// ---------------------------------------------------------
// Load 実装 (統合版)
// ---------------------------------------------------------
void LevelLoader::Load(
    CCL::ECS::Core::World *world, ID3D11Device *device, const std::string &jsonPath)
{
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        std::cerr << "[LevelLoader] Failed to open: " << jsonPath << std::endl;
        return;
    }

    json j;
    file >> j;

    // 1. マップデータの初期化
    int   w    = j.value("width", 20);
    int   h    = j.value("height", 20);
    float size = j.value("cellSize", StageMap::DEFAULT_CELL_SIZE);

    StageMap map;
    map.Initialize(w, h, size);

    // 2. タイルデータの読み込み (互換性対応)

    // パターンA: MapDebugSystem形式 ("layout")
    if (j.contains("layout")) {
        auto &layout = j["layout"];
        for (int i = 0; i < layout.size() && i < map.tiles.size(); ++i) {
            map.tiles[i] = static_cast<TileType>(layout[i]);
        }
    }
    // パターンB: Tiled形式 ("layers")
    else if (j.contains("layers") && !j["layers"].empty()) {
        auto &rawData = j["layers"][0]["data"];
        for (int i = 0; i < rawData.size() && i < map.tiles.size(); ++i) {
            map.tiles[i] = static_cast<TileType>(rawData[i]);
        }
    }

    // 3. 描画用メッシュの構築
    BuildStageMesh(world, device, map);

    // 4. プレイヤーのスポーン
    // 固定マップの場合は、マップの中央手前などをデフォルトにする
    // (もしJSONにSpawnPointがあればそれを使う拡張も可)
    Vector3  spawnPos = {7.0f, 0.0f, 12.0f};
    EntityID playerID = Prefab::SpawnPrefab(*world, "Assets/GamePlay/Prefabs/Player/Player.json", spawnPos);

    CCL::ECS::EntityID gameCameraID =
        Prefab::SpawnPrefab(*world, "Assets/GamePlay/Prefabs/Camera/FollowCamera.json");

    // ★最重要: プレイヤーが見つかったら、カメラのターゲットを上書きする
    if (playerID != 0 && gameCameraID != CCL::ECS::InvalidEntityID) {

        // "追従" ターゲットの設定
        world->PatchComponent<CameraBodyFollow>(
            gameCameraID, [playerID](CameraBodyFollow &body) { body.target = playerID; });

        // "注視" ターゲットの設定
        world->PatchComponent<CameraAimLookAt>(
            gameCameraID, [playerID](CameraAimLookAt &aim) { aim.target = playerID; });
    }

    // 共通化した復元処理を呼ぶ
    RestorePlayerData(world, playerID);

    // 5. オブジェクト層（松明など）の読み込み (Tiled形式の場合)
    if (j.contains("layers") && j["layers"].size() > 1 && j["layers"][1].contains("objects")) {
        for (const auto &obj : j["layers"][1]["objects"]) {
            std::string type = obj["type"];
            int         gx   = obj["x"];
            int         gz   = obj["y"];
            Vector3     pos  = map.GridToWorld(gx, gz);

            if (type == "Torch") {
                // Prefab::Spawn(*world, Prefab::ID::Torch, pos);
            }
        }
    }

    // 6. Worldに登録
    world->AddResource<StageMap>(map);

    world->ScrutinyAndApply();
}



void LevelLoader::DebugLoad(
    CCL::ECS::Core::World *world, ID3D11Device *device, const std::string &jsonPath)
{
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        std::cerr << "[LevelLoader] Failed to open: " << jsonPath << std::endl;
        return;
    }

    json j;
    file >> j;

    // 1. マップデータの初期化
    int   w    = j.value("width", 20);
    int   h    = j.value("height", 20);
    float size = j.value("cellSize", StageMap::DEFAULT_CELL_SIZE);

    StageMap map;
    map.Initialize(w, h, size);

    // 2. タイルデータの読み込み (互換性対応)

    // パターンA: MapDebugSystem形式 ("layout")
    if (j.contains("layout")) {
        auto &layout = j["layout"];
        for (int i = 0; i < layout.size() && i < map.tiles.size(); ++i) {
            map.tiles[i] = static_cast<TileType>(layout[i]);
        }
    }
    // パターンB: Tiled形式 ("layers")
    else if (j.contains("layers") && !j["layers"].empty()) {
        auto &rawData = j["layers"][0]["data"];
        for (int i = 0; i < rawData.size() && i < map.tiles.size(); ++i) {
            map.tiles[i] = static_cast<TileType>(rawData[i]);
        }
    }

    // 3. 描画用メッシュの構築
    BuildStageMesh(world, device, map);

    // 4. プレイヤーのスポーン
    // 固定マップの場合は、マップの中央手前などをデフォルトにする
    // (もしJSONにSpawnPointがあればそれを使う拡張も可)
    //Vector3  spawnPos = map.GridToWorld(w / 2, 2);
    //EntityID playerID = Prefab::Spawn(*world, Prefab::ID::Player, spawnPos);

    // ★共通化した復元処理を呼ぶ
    //RestorePlayerData(world, playerID);

    // 5. オブジェクト層（松明など）の読み込み (Tiled形式の場合)
    if (j.contains("layers") && j["layers"].size() > 1 && j["layers"][1].contains("objects")) {
        for (const auto &obj : j["layers"][1]["objects"]) {
            std::string type = obj["type"];
            int         gx   = obj["x"];
            int         gz   = obj["y"];
            Vector3     pos  = map.GridToWorld(gx, gz);

            if (type == "Torch") {
                // Prefab::Spawn(*world, Prefab::ID::Torch, pos);
            }
        }
    }

    // 6. Worldに登録
    world->AddResource<StageMap>(map);
}

// ---------------------------------------------------------
// LoadProcedural (自動生成版)
// ---------------------------------------------------------
void LevelLoader::LoadProcedural(
    CCL::ECS::Core::World *world, ID3D11Device *device, const MapGenerator::Config &config)
{
    // 1. 生成
    StageMap     map;
    MapGenerator generator;
    generator.Generate(map, config);

    // 2. メッシュ構築
    BuildStageMesh(world, device, map);

    // 3. プレイヤー生成
    EntityID playerID =
        Prefab::SpawnPrefab(*world, "Assets/GamePlay/Prefabs/Player/Player", map.playerSpawnPoint);



     CCL::ECS::EntityID gameCameraID =
        Prefab::SpawnPrefab(*world, "Assets/GamePlay/Prefabs/Camera/FollowCamera.json");

    // ★最重要: プレイヤーが見つかったら、カメラのターゲットを上書きする
    if (playerID != 0 && gameCameraID != CCL::ECS::InvalidEntityID) {

        // "追従" ターゲットの設定
        world->PatchComponent<CameraBodyFollow>(
            gameCameraID, [playerID](CameraBodyFollow &body) { body.target = playerID; });

        // "注視" ターゲットの設定
        world->PatchComponent<CameraAimLookAt>(
            gameCameraID, [playerID](CameraAimLookAt &aim) { aim.target = playerID; });
    }

    // ★共通化した復元処理を呼ぶ
    RestorePlayerData(world, playerID);

    // 4. ゲート生成
    Prefab::SpawnPrefab(*world, "Assets/GamePlay/Prefabs/World/Map/Portal", map.gateSpawnPoint);

    // 5. Worldに登録
    world->AddResource<StageMap>(map);
}



// ---------------------------------------------------------
// Rebuild 実装
// ---------------------------------------------------------
void LevelLoader::Rebuild(CCL::ECS::Core::World *world, ID3D11Device *device)
{
    // 1. 古いマップパーツを全て削除
    // StageMapChunkComponent を持っているEntityを探して削除します
    // ※ECSの仕様に合わせて記述しています。もしDestroyEntityが即時でない場合は、
    //   次のフレームで生成するなど調整が必要ですが、基本はこれで動きます。

// 1. 削除対象を一時リストに集める (イテレータ無効化の回避)
    std::vector<CCL::ECS::EntityID> targets;
    auto mapChip = world->View<StageMapChunkComponent>();
    
    for (auto id : mapChip) {
        targets.push_back(id);
    }

    // 2. ループの外で安全に削除リクエストを出す
    for (auto id : targets) {
        world->Destroy(id);
    }

    // 2. 現在のマップデータを使って再生成
    if (world->HasResource<StageMap>()) {
        const auto &map = world->GetResource<StageMap>();
        BuildStageMesh(world, device, map);
    }
}


