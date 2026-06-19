#pragma once
//============================================================================
//  DungeonLayout.h
//  手作りダンジョンマップ（デバッグ/テスト用）。
//
//  自動生成（DungeonGenerator）と同じ DungeonGenerateResult に変換できるので、
//  DungeonLevelBuilder から先（メッシュ化・グリッド登録・スポーン）は共通。
//
//  「弾・敵のテスト用に固定マップを作り置きする」用途。
//  エディタでセルを編集 → JSON 保存 → 次回ロードして再現、という流れ。
//
//  JSON は nlohmann::json + FileSystem（プロジェクト標準）で入出力する。
//============================================================================
#include "DungeonTypes.h"
#include <string>
#include <vector>

namespace RuneForge::Dungeon
{
    //------------------------------------------------------------------------
    // 手作りマップ。セルは true=壁 / false=床。スポーンは種類付きセル。
    //------------------------------------------------------------------------
    class DungeonLayout
    {
    public:
        int   width = 21;
        int   depth = 21;
        float cellSize = 2.0f;
        float originX = 0.0f;
        float originZ = 0.0f;

        // solid[i][j]。true=壁。既定は外周だけ壁の空き部屋。
        std::vector<std::vector<bool>> solid;

        // スポーンマーカー（敵・宝箱・スタート・ポータル等）
        std::vector<SpawnMarker> markers;

        // スタート / ポータルのセル（プレイヤー生成・ゴール用）
        CellPos startCell{ 1, 1 };
        CellPos portalCell{ 1, 1 };

        DungeonLayout() { Reset(width, depth); }

        // 指定サイズで「外周のみ壁・内部は床」の初期状態にする
        void Reset(int w, int h)
        {
            width = (w < 3) ? 3 : w;
            depth = (h < 3) ? 3 : h;
            solid.assign(width, std::vector<bool>(depth, false));
            for (int i = 0; i < width; ++i) { solid[i][0] = true; solid[i][depth - 1] = true; }
            for (int j = 0; j < depth; ++j) { solid[0][j] = true; solid[width - 1][j] = true; }
            markers.clear();
            startCell = { 1, 1 };
            portalCell = { width - 2, depth - 2 };
        }

        bool InBounds(int i, int j) const
        {
            return i >= 0 && j >= 0 && i < width && j < depth;
        }

        // --- DungeonGenerateResult への変換（共通パイプラインへ流す入口）---
        DungeonGenerateResult ToResult(uint32_t tagSeed = 0) const
        {
            DungeonGenerateResult r;
            r.width = width;
            r.depth = depth;
            r.cellSize = cellSize;
            r.originX = originX;
            r.originZ = originZ;
            r.solid = solid;
            r.startCell = startCell;
            r.portalCell = portalCell;
            r.usedSeed = tagSeed;

            // 1部屋扱いで全体を rooms に入れ、マーカーをぶら下げる
            RoomMeta room;
            room.type = RoomType::Combat;
            room.minI = 0; room.minJ = 0;
            room.maxI = width - 1; room.maxJ = depth - 1;
            room.spawnMarkers = markers;
            // スタート/ポータルをマーカーにも保証
            room.spawnMarkers.push_back({ startCell,  SpawnMarkerType::StartPoint });
            room.spawnMarkers.push_back({ portalCell, SpawnMarkerType::Portal });
            r.rooms.push_back(room);
            return r;
        }

        // --- JSON 入出力（FileSystem 経由）---
        //   保存先は仮想パス（例 "Assets/DebugMaps/test1.json"）。
        bool SaveToJson(const std::string& virtualPath) const;
        bool LoadFromJson(const std::string& virtualPath);
    };

} // namespace RuneForge::Dungeon