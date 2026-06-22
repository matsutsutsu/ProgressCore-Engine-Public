/// @file  NavGridFlowField.h
/// @brief プレイヤーのセルから全セルへの「距離場（フローフィールド）」を保持する Resource。
///
/// NavFlowFieldSystem が毎フレーム BFS で埋め、EnemyMovementSystem が読む。
/// 敵は「自分のセルの隣で距離が最小のセル」へ歩くだけで、壁を避けて最短でプレイヤーへ向かう。
///
/// メモリ: dist は width*depth の int 配列。-1 = 到達不可（壁・分断）。
#pragma once
#include <vector>
#include <cstdint>

namespace RuneForge::Dungeon
{
    /// @brief グリッド上の「プレイヤーまでの歩数」を格納する距離場。
    struct NavGridFlowField
    {
        int width = 0;   ///< X方向セル数（DungeonGrid と一致）
        int depth = 0;   ///< Z方向セル数

        /// @brief dist[i*depth + j] = そのセルからプレイヤーセルまでの歩数。-1 = 到達不可。
        std::vector<int32_t> dist;

        bool valid = false;  ///< この距離場が今フレーム有効か
        int  targetI = -1;     ///< 距離0のセル（プレイヤーのセル）
        int  targetJ = -1;

        /// @brief セル(i,j)の距離を返す。範囲外・壁は -1。
        int Dist(int i, int j) const
        {
            if (i < 0 || j < 0 || i >= width || j >= depth) return -1;
            return dist[static_cast<size_t>(i) * depth + j];
        }
    };

} // namespace RuneForge::Dungeon