#pragma once
//============================================================================
//  DungeonGenerator.h
//  RuneForge ダンジョン生成 — 純粋生成ロジック（仕様書 §4, §7）
//
//    DungeonGenerateResult Generate(uint32_t seed, int floorIndex);
//      - 同じシードなら必ず同じ結果（決定的）
//      - usedSeed に使ったシードを記録
//
//  【生成方式】グリッドグロース型（Enter the Gungeon 風）
//    先に部屋の繋がり（グラフ）を仮想グリッド上で上下左右に成長させて決め、
//    各ノードに部屋テンプレートを当てはめ、隣接部屋を通路で繋ぐ。
//    縦長にならず二次元的に広がる。スタートとゴールはグラフ上で最も離れる。
//============================================================================
#include "DungeonTypes.h"
#include <string>
#include <cstdint>

namespace RuneForge::Dungeon
{
    //------------------------------------------------------------------------
    // 生成パラメータ（デバッグ/エディタから調整できる）
    //   既定値は「8〜12部屋・バランス型分岐・Gungeon風（通路で明確に接続）」。
    //------------------------------------------------------------------------
    struct DungeonGenParams
    {
        // --- 部屋数（層ごとに増やしたい場合は floorBonus を使う）---
        int minRooms = 8;
        int maxRooms = 12;
        int roomsPerFloorBonus = 1; // floorIndex-1 ごとに min/max に加算

        // --- 迷宮の性格 ---
        // 1部屋から枝が増える確率。高いほど分岐が多い迷宮になる。
        //   0.0 ≒ 一本道、0.35 ≒ バランス、0.6+ ≒ 迷宮的
        float branchChance = 0.35f;
        // 1部屋から伸ばす最大方向数（主経路含む、1〜4）
        int   maxBranchPerRoom = 2;
        // 成長中に同じ部屋からさらに伸ばすため frontier に戻す確率
        float regrowChance = 0.30f;

        // --- 通路 ---
        int corridorWidth = 3; // 通路の幅（2〜3推奨）

        // --- 部屋セルの実タイル寸法 ---
        //   1部屋セルが占める実グリッドの最大寸法。テンプレートはこの中に収める。
        int cellTilesW = 13; // 1部屋あたりの横タイル数（テンプレ最大幅+余白）
        int cellTilesH = 13; // 1部屋あたりの縦タイル数
        int cellGap = 3;  // 部屋セル間の余白（通路が通る帯）

        // 必須部屋を配置するか（ショップ/鍛冶/宝物庫/エリート）
        bool placeSpecialRooms = true;
    };

    class DungeonGenerator
    {
    public:
        // floorIndex は 1〜5 を想定（範囲外は内部でクランプ）。
        static DungeonGenerateResult Generate(uint32_t seed, int floorIndex);

        // パラメータ指定版（デバッグ/エディタ用）。
        static DungeonGenerateResult Generate(uint32_t seed, int floorIndex,
            const DungeonGenParams& params);

        // ASCII アート出力。Logger があればそちらへ、無ければ std::printf へ。
        static void DebugPrintGrid(const DungeonGenerateResult& result);

        // ASCII アートを文字列として取得（UIやテストで使える）。
        static std::string BuildAsciiArt(const DungeonGenerateResult& result);
    };

} // namespace RuneForge::Dungeon