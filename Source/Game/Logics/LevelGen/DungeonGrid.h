#pragma once
//============================================================================
//  DungeonGrid.h
//  ダンジョンの「当たり判定・問い合わせ」用リソース。
//
//  DungeonGenerateResult の solid グリッドを保持し、World にリソースとして
//  登録して使う（前回チームの StageMap と同じ役割）。
//  弾・敵・AI はこのリソースを参照して壁判定や経路探索を行う。
//
//  使い方:
//    world.AddResource<DungeonGrid>(DungeonGrid::FromResult(genResult));
//    ...
//    auto& grid = world.GetResource<DungeonGrid>();
//    if (grid.IsWallAtWorld(x, z)) { ... }
//============================================================================
#include "DungeonTypes.h"
#include <vector>
#include <cmath>

namespace RuneForge::Dungeon
{
    // DDA レイキャスト結果（弾の壁反射などに使う）
    struct GridRayHit
    {
        bool  hit = false;
        float distance = 0.0f;     // 始点からの距離（ワールド単位）
        float hitX = 0.0f;     // 衝突点ワールド座標
        float hitZ = 0.0f;
        float normalX = 0.0f;     // 面法線（X or Z のどちらか ±1、他は0）
        float normalZ = 0.0f;
        CellPos cell;              // 衝突した壁セル
    };

    class DungeonGrid
    {
    public:
        DungeonGrid() = default;

        // 生成結果からグリッドを構築
        static DungeonGrid FromResult(const DungeonGenerateResult& r)
        {
            DungeonGrid g;
            g._width = r.width;
            g._depth = r.depth;
            g._cellSize = r.cellSize;
            g._originX = r.originX;
            g._originZ = r.originZ;
            g._solid = r.solid;
            g._startCell = r.startCell;
            g._portalCell = r.portalCell;
            return g;
        }

        // ---- 基本情報 ----
        int   Width()    const { return _width; }
        int   Depth()    const { return _depth; }
        float CellSize() const { return _cellSize; }
        CellPos StartCell()  const { return _startCell; }
        CellPos PortalCell() const { return _portalCell; }

        // ---- 座標変換 ----
        // セル中心のワールド座標
        float CellCenterWorldX(int i) const { return _originX + (i + 0.5f) * _cellSize; }
        float CellCenterWorldZ(int j) const { return _originZ + (j + 0.5f) * _cellSize; }

        // ワールド座標 → セル（範囲外でも添字は返す。判定は IsWall 側で）
        void WorldToCell(float worldX, float worldZ, int& outI, int& outJ) const
        {
            outI = (int)std::floor((worldX - _originX) / _cellSize);
            outJ = (int)std::floor((worldZ - _originZ) / _cellSize);
        }

        // ---- 壁判定 ----
        // セル単位（範囲外は壁扱い）
        bool IsWall(int i, int j) const
        {
            if (i < 0 || j < 0 || i >= _width || j >= _depth) return true;
            return _solid[i][j];
        }
        // ワールド座標
        bool IsWallAtWorld(float worldX, float worldZ) const
        {
            int i, j; WorldToCell(worldX, worldZ, i, j);
            return IsWall(i, j);
        }

        // ---- DDA レイキャスト（弾の壁衝突・反射用、仕様書 §10）----
        //   始点(ox,oz)から方向(dx,dz)へ maxDist まで進み、最初に当たる壁面を返す。
        //   壁は軸平行なので法線は X か Z 方向のみ。
        GridRayHit RaycastDDA(float ox, float oz, float dx, float dz, float maxDist) const
        {
            GridRayHit out;

            // 方向正規化
            float len = std::sqrt(dx * dx + dz * dz);
            if (len < 1e-6f) return out;
            dx /= len; dz /= len;

            // 現在セル
            int i, j; WorldToCell(ox, oz, i, j);

            // 始点が既に壁の中なら即ヒット扱い
            if (IsWall(i, j)) {
                out.hit = true; out.distance = 0.0f;
                out.hitX = ox; out.hitZ = oz; out.cell = { i, j };
                return out;
            }

            // セル境界までの距離・進行ステップ（標準的なグリッドDDA）
            const float cs = _cellSize;
            int stepI = (dx > 0) ? 1 : -1;
            int stepJ = (dz > 0) ? 1 : -1;

            // 次のX境界・Z境界までのワールド距離
            float nextBoundX = _originX + ((dx > 0) ? (i + 1) : i) * cs;
            float nextBoundZ = _originZ + ((dz > 0) ? (j + 1) : j) * cs;

            float tMaxX = (std::fabs(dx) > 1e-6f) ? (nextBoundX - ox) / dx : 1e30f;
            float tMaxZ = (std::fabs(dz) > 1e-6f) ? (nextBoundZ - oz) / dz : 1e30f;
            float tDeltaX = (std::fabs(dx) > 1e-6f) ? cs / std::fabs(dx) : 1e30f;
            float tDeltaZ = (std::fabs(dz) > 1e-6f) ? cs / std::fabs(dz) : 1e30f;

            float t = 0.0f;
            int   lastAxis = 0; // 0 = X面, 1 = Z面

            while (t <= maxDist)
            {
                if (tMaxX < tMaxZ) { t = tMaxX; tMaxX += tDeltaX; i += stepI; lastAxis = 0; }
                else { t = tMaxZ; tMaxZ += tDeltaZ; j += stepJ; lastAxis = 1; }

                if (t > maxDist) break;

                if (IsWall(i, j))
                {
                    out.hit = true;
                    out.distance = t;
                    out.hitX = ox + dx * t;
                    out.hitZ = oz + dz * t;
                    out.cell = { i, j };
                    // 進入してきた面の法線（進行方向と逆向き）
                    if (lastAxis == 0) { out.normalX = (stepI > 0) ? -1.0f : 1.0f; out.normalZ = 0.0f; }
                    else { out.normalX = 0.0f; out.normalZ = (stepJ > 0) ? -1.0f : 1.0f; }
                    return out;
                }
            }
            return out; // ヒットなし
        }

    private:
        int   _width = 0, _depth = 0;
        float _cellSize = 2.0f, _originX = 0.0f, _originZ = 0.0f;
        std::vector<std::vector<bool>> _solid;
        CellPos _startCell, _portalCell;
    };

} // namespace RuneForge::Dungeon