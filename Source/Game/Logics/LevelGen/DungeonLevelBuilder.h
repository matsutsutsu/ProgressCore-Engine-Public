#pragma once
//============================================================================
//  DungeonLevelBuilder.h
//  RuneForge ダンジョン — 生成結果を 3D エンティティとしてシーンに構築する。
//
//  責務:
//    DungeonGenerator（純粋ロジック）が返す DungeonGenerateResult を受け取り、
//    チャンク単位でメッシュ結合 → Model 化 → ECS エンティティ生成 までを担う。
//    SceneInitializer から独立させ、ダンジョン構築の責務をここに集約する。
//
//  使い方:
//    DungeonLevelBuilder::Build(world, device, seed, floorIndex);
//
//    モデルパスやチャンクサイズを変えたい場合は BuildParams を渡す:
//    DungeonLevelBuilder::BuildParams p;
//    p.floorModelPath = "...";
//    DungeonLevelBuilder::Build(world, device, p);
//============================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/LevelGen/DungeonTypes.h"      // DungeonGenerateResult
#include "Game/Logics/LevelGen/DungeonGenerator.h"  // DungeonGenParams
#include <cstdint>
#include <string>

struct ID3D11Device;

class DungeonLevelBuilder
{
public:
    // 構築パラメータ（既定値はプロジェクト構成に合わせて調整）
    struct BuildParams
    {
        uint32_t    seed = 12345;
        int         floorIndex = 1;
        int         chunkSize = 16;   // 1チャンクの一辺（セル数）

        // ★ 生成アルゴリズムのパラメータ（部屋数・分岐・通路幅をデバッグ調整）
        RuneForge::Dungeon::DungeonGenParams gen;

        std::string floorModelPath = "Assets/Models/MapChip/Floor.glb";
        std::string wallModelPath = "Assets/Models/MapChip/Wall.glb";
        bool        applyImmediately = true; // 末尾で world.ScrutinyAndApply() するか

        // ★ グリッドを衝突用リソース(DungeonGrid)として World に登録するか
        bool        registerGridResource = true;

        // ★ スポーンマーカーから Prefab を配置するか
        bool        spawnPrefabs = true;
        // 各マーカー種に対応する Prefab パス（空なら配置しない）
        std::string enemyPrefab = "Assets/Prefabs/Enemy/EnemyTest.json";
        std::string chestPrefab = "Assets/Prefabs/Item/Chest.json";
        std::string shopPrefab = "Assets/Prefabs/Shop/Shop.json";
        std::string forgePrefab = "Assets/Prefabs/Forge/Forge.json";
        std::string portalPrefab = "Assets/Prefabs/Portal/Portal.json";
        std::string bossPrefab = "Assets/Prefabs/Boss/Boss.json";
        // StartPoint はプレイヤー配置に使うことが多いので既定では Prefab を出さない
        std::string playerPrefab = ""; // 空なら配置しない（呼び出し側で別途配置する想定）
    };

    // 簡易版（seed/floor だけ指定、他は既定値）
    static void Build(CCL::ECS::Core::World& world, ID3D11Device* device,
        uint32_t seed = 12345, int floorIndex = 1);

    // 詳細版
    static void Build(CCL::ECS::Core::World& world, ID3D11Device* device,
        const BuildParams& params);

    // 既に作られた DungeonGenerateResult を直接構築する（共通パイプライン）。
    //   自動生成・手作りマップ（DungeonLayout::ToResult）のどちらも最終的にここを通る。
    //   手作りマップを建てたいときはこれを呼ぶ。
    static void BuildFromResult(CCL::ECS::Core::World& world, ID3D11Device* device,
                      const RuneForge::Dungeon::DungeonGenerateResult& result,
                      const BuildParams& params);

    // 既存のダンジョンチャンクを全削除する（再生成前に呼ぶ）。
    static void Clear(CCL::ECS::Core::World& world);

    // ------------------------------------------------------------------
    // プレイヤー生成（ダンジョンとは別ライフサイクル）
    //   Build() ではプレイヤーを生成しない（特殊生成や層遷移で持ち越すため）。
    //   通常プレイ開始時にこれを呼んでスタート地点にプレイヤーを配置する。
    //
    //   グリッドのスタートセル（DungeonGrid::StartCell）のワールド座標に
    //   Player.json を生成し、フォローカメラもセットで生成・ターゲット設定する。
    //
    //   戻り値: 生成したプレイヤーの EntityID（失敗時は InvalidEntityID）。
    // ------------------------------------------------------------------
    struct PlayerSpawnParams
    {
        std::string playerPrefab = "Assets/Prefabs/Player/PlayerTest.json";
        std::string cameraPrefab = "Assets/Prefabs/Camera/FollowCamera.json";
        bool        spawnCamera = true;   // フォローカメラもセットで生成するか
        float       spawnYOffset = 1.0f;   // 床から少し浮かせる（埋まり防止）
    };

    // DungeonGrid リソースを使ってスタート地点に配置（Build 後に呼ぶ）。
    static CCL::ECS::EntityID SpawnPlayerAtStart(
        CCL::ECS::Core::World& world, const PlayerSpawnParams& params);
    // 既定パラメータ版
    static CCL::ECS::EntityID SpawnPlayerAtStart(CCL::ECS::Core::World& world)
    {
        return SpawnPlayerAtStart(world, PlayerSpawnParams{});
    }

    // 任意のワールド座標に配置（スタート以外に出したいとき用）。
    static CCL::ECS::EntityID SpawnPlayerAt(
        CCL::ECS::Core::World& world,
        float worldX, float worldY, float worldZ,
        const PlayerSpawnParams& params);
    // 既定パラメータ版
    static CCL::ECS::EntityID SpawnPlayerAt(
        CCL::ECS::Core::World& world, float worldX, float worldY, float worldZ)
    {
        return SpawnPlayerAt(world, worldX, worldY, worldZ, PlayerSpawnParams{});
    }

    // 診断ログの ON/OFF（既定 false）。デバッグ時のみ true にする。
    static void SetDiagnosticLogging(bool enabled) { s_diagnosticLogging = enabled; }
    static bool IsDiagnosticLogging() { return s_diagnosticLogging; }

private:
    static bool s_diagnosticLogging;

    // スポーンマーカーから Prefab を配置する内部ヘルパ
    static void SpawnMarkers(CCL::ECS::Core::World& world,
        const RuneForge::Dungeon::DungeonGenerateResult& result,
        const BuildParams& params);
};