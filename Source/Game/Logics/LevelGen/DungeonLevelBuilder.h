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
#include "Game/Logics/LevelGen/DungeonTypes.h"   // DungeonGenerateResult
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
        std::string floorModelPath = "Assets/Models/MapChip/Floor.glb";
        std::string wallModelPath = "Assets/Models/MapChip/Wall.glb";
        bool        applyImmediately = true; // 末尾で world.ScrutinyAndApply() するか

        // ★ グリッドを衝突用リソース(DungeonGrid)として World に登録するか
        bool        registerGridResource = true;

        // ★ スポーンマーカーから Prefab を配置するか
        bool        spawnPrefabs = true;
        // 各マーカー種に対応する Prefab パス（空なら配置しない）
        std::string enemyPrefab = "Assets/Prefabs/Enemy/Enemy.json";
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

    // 既存のダンジョンチャンクを全削除する（再生成前に呼ぶ）。
    static void Clear(CCL::ECS::Core::World& world);

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