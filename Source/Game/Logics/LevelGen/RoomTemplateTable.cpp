//============================================================================
//  RoomTemplateTable.cpp
//============================================================================
#include "RoomTemplateTable.h"

namespace RuneForge::Dungeon
{
    //------------------------------------------------------------------------
    // テンプレート構築ヘルパ
    //   行優先の文字列レイアウトから solidMask を作る。
    //   各 row は Z の一行（j 固定）、文字の並びが X 方向（i 増加）。
    //   '#' = 壁, '.' = 床
    //   ※ rows[0] が j=0、文字index が i に対応する。
    //   solidMask[x][z] にマップし直す。
    //------------------------------------------------------------------------
    static std::vector<std::vector<bool>> MaskFromRows(
        const std::vector<const char*>& rows, int w, int h)
    {
        std::vector<std::vector<bool>> mask(w, std::vector<bool>(h, false));
        for (int z = 0; z < h; ++z)
        {
            const char* row = rows[z];
            for (int x = 0; x < w; ++x)
            {
                char c = row[x];
                mask[x][z] = (c == '#');
            }
        }
        return mask;
    }

    //------------------------------------------------------------------------
    // テンプレートテーブル本体
    //   外周は基本的に壁（'#'）、ドア部分の開口は通路接続フェーズで穴を開ける。
    //   ドアは「外周の壁セルのうち、外側に通路を伸ばす起点」を指す。
    //------------------------------------------------------------------------
    const std::vector<RoomTemplate>& GetRoomTemplateTable()
    {
        static const std::vector<RoomTemplate> table = []
            {
                std::vector<RoomTemplate> t;

                //================================================================
                // スタート部屋 (Start) : 7x7 開けた部屋、1ドア(+Z)
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Start;
                    r.width = 7; r.height = 7;
                    r.solidMask = MaskFromRows({
                        "#######",
                        "#.....#",
                        "#.....#",
                        "#.....#",
                        "#.....#",
                        "#.....#",
                        "#######",
                        }, 7, 7);
                    // 上辺 (+Z 方向 = j 最大) の中央にドア
                    r.doors = { { {3, 6}, 2 } };
                    r.spawnMarkers = { { {3, 3}, SpawnMarkerType::StartPoint } };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                //================================================================
                // 戦闘部屋: 開放アリーナ (Combat) : 9x9 中央が広い、4ドア
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Combat;
                    r.width = 9; r.height = 9;
                    r.solidMask = MaskFromRows({
                        "#########",
                        "#.......#",
                        "#.......#",
                        "#.......#",
                        "#.......#",
                        "#.......#",
                        "#.......#",
                        "#.......#",
                        "#########",
                        }, 9, 9);
                    r.doors = {
                        { {8, 4}, 0 }, // +X
                        { {0, 4}, 1 }, // -X
                        { {4, 8}, 2 }, // +Z
                        { {4, 0}, 3 }, // -Z
                    };
                    r.spawnMarkers = {
                        { {2, 2}, SpawnMarkerType::Enemy },
                        { {6, 2}, SpawnMarkerType::Enemy },
                        { {2, 6}, SpawnMarkerType::Enemy },
                        { {6, 6}, SpawnMarkerType::Enemy },
                        { {4, 4}, SpawnMarkerType::Enemy },
                    };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                //================================================================
                // 戦闘部屋: 柱だらけ (Combat) : 9x9 内部に四角柱（貫通ビルド有利）
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Combat;
                    r.width = 9; r.height = 9;
                    r.solidMask = MaskFromRows({
                        "#########",
                        "#.......#",
                        "#.#.#.#.#",
                        "#.......#",
                        "#.#.#.#.#",
                        "#.......#",
                        "#.#.#.#.#",
                        "#.......#",
                        "#########",
                        }, 9, 9);
                    r.doors = {
                        { {8, 4}, 0 },
                        { {0, 4}, 1 },
                        { {4, 8}, 2 },
                        { {4, 0}, 3 },
                    };
                    r.spawnMarkers = {
                        { {1, 1}, SpawnMarkerType::Enemy },
                        { {7, 1}, SpawnMarkerType::Enemy },
                        { {1, 7}, SpawnMarkerType::Enemy },
                        { {7, 7}, SpawnMarkerType::Enemy },
                    };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                //================================================================
                // 戦闘部屋: 狭い回廊 (Combat) : 11x5 通路状（直進ビルド向き）2ドア
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Combat;
                    r.width = 11; r.height = 5;
                    r.solidMask = MaskFromRows({
                        "###########",
                        "#.........#",
                        "#.........#",
                        "#.........#",
                        "###########",
                        }, 11, 5);
                    r.doors = {
                        { {10, 2}, 0 }, // +X
                        { {0, 2}, 1 },  // -X
                    };
                    r.spawnMarkers = {
                        { {3, 2}, SpawnMarkerType::Enemy },
                        { {5, 2}, SpawnMarkerType::Enemy },
                        { {7, 2}, SpawnMarkerType::Enemy },
                    };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                //================================================================
                // ショップ (Shop) : 5x5 1ドア部屋
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Shop;
                    r.width = 5; r.height = 5;
                    r.solidMask = MaskFromRows({
                        "#####",
                        "#...#",
                        "#...#",
                        "#...#",
                        "#####",
                        }, 5, 5);
                    r.doors = { { {2, 0}, 3 } }; // -Z に1ドア
                    r.spawnMarkers = { { {2, 2}, SpawnMarkerType::Shop } };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                //================================================================
                // 鍛冶場 (Forge) : 5x5 1ドア部屋
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Forge;
                    r.width = 5; r.height = 5;
                    r.solidMask = MaskFromRows({
                        "#####",
                        "#...#",
                        "#...#",
                        "#...#",
                        "#####",
                        }, 5, 5);
                    r.doors = { { {2, 0}, 3 } };
                    r.spawnMarkers = { { {2, 2}, SpawnMarkerType::Forge } };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                //================================================================
                // 宝物庫 (Treasury) : 5x5 1ドア、宝箱配置
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Treasury;
                    r.width = 5; r.height = 5;
                    r.solidMask = MaskFromRows({
                        "#####",
                        "#...#",
                        "#...#",
                        "#...#",
                        "#####",
                        }, 5, 5);
                    r.doors = { { {2, 0}, 3 } };
                    r.spawnMarkers = {
                        { {2, 2}, SpawnMarkerType::Chest },
                        { {1, 3}, SpawnMarkerType::Chest },
                        { {3, 3}, SpawnMarkerType::Chest },
                    };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                //================================================================
                // エリート部屋 (Elite) : 7x7 中ボス級、2ドア
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Elite;
                    r.width = 7; r.height = 7;
                    r.solidMask = MaskFromRows({
                        "#######",
                        "#.....#",
                        "#.....#",
                        "#.....#",
                        "#.....#",
                        "#.....#",
                        "#######",
                        }, 7, 7);
                    r.doors = {
                        { {0, 3}, 1 },  // -X
                        { {6, 3}, 0 },  // +X
                    };
                    r.spawnMarkers = {
                        { {3, 3}, SpawnMarkerType::Enemy }, // エリート本体
                        { {3, 4}, SpawnMarkerType::Chest },
                    };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                //================================================================
                // ポータル部屋 (Portal) : 7x7 1ドア、脱出ポータル
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Portal;
                    r.width = 7; r.height = 7;
                    r.solidMask = MaskFromRows({
                        "#######",
                        "#.....#",
                        "#.....#",
                        "#.....#",
                        "#.....#",
                        "#.....#",
                        "#######",
                        }, 7, 7);
                    r.doors = { { {3, 0}, 3 } }; // -Z 側から入る
                    r.spawnMarkers = { { {3, 3}, SpawnMarkerType::Portal } };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                //================================================================
                // ボス部屋 (Boss) : 11x11 大部屋、1ドア（第5層最深部）
                //================================================================
                {
                    RoomTemplate r;
                    r.type = RoomType::Boss;
                    r.width = 11; r.height = 11;
                    r.solidMask = MaskFromRows({
                        "###########",
                        "#.........#",
                        "#.........#",
                        "#.........#",
                        "#.........#",
                        "#.........#",
                        "#.........#",
                        "#.........#",
                        "#.........#",
                        "#.........#",
                        "###########",
                        }, 11, 11);
                    r.doors = { { {5, 0}, 3 } }; // -Z 側から入る
                    r.spawnMarkers = {
                        { {5, 6}, SpawnMarkerType::Boss },
                        { {5, 8}, SpawnMarkerType::Portal }, // ボス撃破後に出現する想定
                    };
                    r.allowedRotations = { 0, 90, 180, 270 };
                    t.push_back(r);
                }

                return t;
            }();

        return table;
    }

    //------------------------------------------------------------------------
    std::vector<int> GetTemplatesForType(RoomType type)
    {
        std::vector<int> out;
        const auto& table = GetRoomTemplateTable();
        for (int idx = 0; idx < (int)table.size(); ++idx)
            if (table[idx].type == type)
                out.push_back(idx);
        return out;
    }

    //------------------------------------------------------------------------
    // 90°単位回転
    //   座標変換 (x,z) -> 新(x',z')。CW 90° ごと。
    //   新しい width/height、solidMask、doors、spawnMarkers を回す。
    //------------------------------------------------------------------------
    static CellPos RotateCell(int x, int z, int w, int h, int rotationDeg)
    {
        switch (((rotationDeg % 360) + 360) % 360)
        {
        case 90:  return { h - 1 - z, x };          // (x,z)->(h-1-z, x)
        case 180: return { w - 1 - x, h - 1 - z };
        case 270: return { z, w - 1 - x };
        default:  return { x, z };                  // 0
        }
    }

    // ドア方向の回転 ( 0=+X,1=-X,2=+Z,3=-Z )
    static int RotateDir(int dir, int rotationDeg)
    {
        // 方向ベクトル: 0:+X(1,0) 1:-X(-1,0) 2:+Z(0,1) 3:-Z(0,-1)
        static const int dx[4] = { 1, -1, 0, 0 };
        static const int dz[4] = { 0, 0, 1, -1 };
        int x = dx[dir], z = dz[dir];
        int steps = (((rotationDeg % 360) + 360) % 360) / 90;
        for (int s = 0; s < steps; ++s)
        {
            // CW 90°: (x,z) -> (z, -x) は座標系で要調整。
            // RotateCell と整合するように回す: (x,z) -> (-z, x)
            int nx = -z;
            int nz = x;
            x = nx; z = nz;
        }
        for (int d = 0; d < 4; ++d)
            if (dx[d] == x && dz[d] == z) return d;
        return dir;
    }

    RoomTemplate RotateTemplate(const RoomTemplate& src, int rotationDeg)
    {
        int rot = (((rotationDeg % 360) + 360) % 360);
        if (rot == 0) return src;

        RoomTemplate out;
        out.type = src.type;
        out.allowedRotations = src.allowedRotations;

        bool swap = (rot == 90 || rot == 270);
        out.width = swap ? src.height : src.width;
        out.height = swap ? src.width : src.height;

        out.solidMask.assign(out.width, std::vector<bool>(out.height, false));
        for (int z = 0; z < src.height; ++z)
            for (int x = 0; x < src.width; ++x)
            {
                CellPos n = RotateCell(x, z, src.width, src.height, rot);
                out.solidMask[n.i][n.j] = src.solidMask[x][z];
            }

        for (const auto& d : src.doors)
        {
            CellPos n = RotateCell(d.cell.i, d.cell.j, src.width, src.height, rot);
            out.doors.push_back({ n, RotateDir(d.direction, rot) });
        }
        for (const auto& m : src.spawnMarkers)
        {
            CellPos n = RotateCell(m.cell.i, m.cell.j, src.width, src.height, rot);
            out.spawnMarkers.push_back({ n, m.type });
        }
        return out;
    }

} // namespace RuneForge::Dungeon