#pragma once
//============================================================================
//  DungeonTypes.h
//  RuneForge ダンジョン生成 — 出力データ構造（仕様書 §3 厳守）
//
//  この構造体群は「統合側（ECS / Jolt / レンダリング）が受け取って使う」もの。
//  生成側は中身を正しく埋めるだけ。フィールド名・意味は変更しないこと。
//============================================================================
#include <cstdint>
#include <vector>

namespace RuneForge::Dungeon
{
    //------------------------------------------------------------------------
    // セル座標 ( i = X方向, j = Z方向 )
    //------------------------------------------------------------------------
    struct CellPos
    {
        int i = 0;
        int j = 0;
    };

    //------------------------------------------------------------------------
    // スポーンマーカーの種類
    //------------------------------------------------------------------------
    enum class SpawnMarkerType
    {
        Enemy,
        Chest,
        Portal,
        Boss,
        Shop,
        Forge,
        StartPoint
    };

    //------------------------------------------------------------------------
    // 部屋カテゴリ
    //------------------------------------------------------------------------
    enum class RoomType
    {
        Start,
        Combat,
        Shop,
        Treasury,
        Forge,
        Elite,
        Portal,
        Boss
    };

    //------------------------------------------------------------------------
    // スポーン候補セル
    //------------------------------------------------------------------------
    struct SpawnMarker
    {
        CellPos         cell;
        SpawnMarkerType type;
    };

    //------------------------------------------------------------------------
    // ドア情報   direction: 0=+X, 1=-X, 2=+Z, 3=-Z
    //------------------------------------------------------------------------
    struct DoorInfo
    {
        CellPos cell;
        int     direction = 0;
    };

    //------------------------------------------------------------------------
    // 部屋メタ情報
    //------------------------------------------------------------------------
    struct RoomMeta
    {
        RoomType                 type = RoomType::Combat;
        int                      minI = 0, minJ = 0, maxI = 0, maxJ = 0; // セル矩形（両端含む）
        std::vector<DoorInfo>    doors;
        std::vector<SpawnMarker> spawnMarkers;
    };

    //------------------------------------------------------------------------
    // 生成結果（これが生成関数の戻り値）
    //------------------------------------------------------------------------
    struct DungeonGenerateResult
    {
        // --- グリッド（唯一の真実源） ---
        int   width = 0;     // X方向のセル数
        int   depth = 0;     // Z方向のセル数
        float cellSize = 2.0f;  // 1セルのワールド単位（メートル）
        float originX = 0.0f;  // セル(0,0)の隅のワールドX座標
        float originZ = 0.0f;  // セル(0,0)の隅のワールドZ座標
        // 変換: worldX = originX + i * cellSize, worldZ = originZ + j * cellSize
        std::vector<std::vector<bool>> solid; // solid[i][j]。true = 壁

        // --- 部屋情報 ---
        std::vector<RoomMeta> rooms;

        // --- グローバル情報 ---
        CellPos  startCell;     // プレイヤー開始セル
        CellPos  portalCell;    // 脱出ポータルセル（最深部）
        uint32_t usedSeed = 0;  // 再現用シード

        // 便利アクセサ（範囲外は壁扱い）
        bool IsSolid(int i, int j) const
        {
            if (i < 0 || j < 0 || i >= width || j >= depth) return true;
            return solid[i][j];
        }
    };

} // namespace RuneForge::Dungeon