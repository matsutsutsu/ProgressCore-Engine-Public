#pragma once
//============================================================================
//  DungeonGenerator.h
//  RuneForge ダンジョン生成 — 純粋生成ロジック（仕様書 §4, §7）
//
//    DungeonGenerateResult Generate(uint32_t seed, int floorIndex);
//      - 同じシードなら必ず同じ結果（決定的）
//      - usedSeed に使ったシードを記録
//
//    void DebugPrintGrid(const DungeonGenerateResult&);
//      - グリッドを ASCII アートで Logger / コンソールに出力
//============================================================================
#include "DungeonTypes.h"
#include <string>

namespace RuneForge::Dungeon
{
    class DungeonGenerator
    {
    public:
        // floorIndex は 1〜5 を想定（範囲外は内部でクランプ）。
        static DungeonGenerateResult Generate(uint32_t seed, int floorIndex);

        // ASCII アート出力。Logger があればそちらへ、無ければ std::printf へ。
        static void DebugPrintGrid(const DungeonGenerateResult& result);

        // ASCII アートを文字列として取得（UIやテストで使える）。
        static std::string BuildAsciiArt(const DungeonGenerateResult& result);
    };

} // namespace RuneForge::Dungeon