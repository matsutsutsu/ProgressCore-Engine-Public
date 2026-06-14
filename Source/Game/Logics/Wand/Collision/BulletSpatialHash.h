#pragma once
// ===========================================================================
// Registry/BulletSpatialHash.h
// ----------------------------
// @file
// @brief 弾×敵判定用の空間ハッシュ（World Resource）。
//        毎フレーム「敵 System が BuildPhase で構築」→「弾 System が QueryPhase で参照」。
//
// 設計:
//   セルサイズ = DungeonGrid::CellSize() と同一。
//   キー = (i, j) を整数ハッシュ化。1セルに複数敵が入れる（vector バケツ）。
//   毎フレーム Clear→Build→Query の順で使う。
// ===========================================================================
#include <unordered_map>
#include <vector>
#include "Engine/ECS/Common/CCL_Types.h"
#include "SimpleMath.h"

/**
 * @brief 毎フレーム再構築する敵の空間ハッシュ（弾との判定に使う）。
 */
class BulletSpatialHash
{
public:
    struct EnemyEntry
    {
        CCL::ECS::EntityID id;
        float worldX, worldZ; ///< 敵の中心位置
        float radius;         ///< 当たり判定半径
    };

    /// @brief フレーム開始時に呼ぶ（敵登録System が呼ぶ）。
    void Clear() { _buckets.clear(); }

    /// @brief 敵1体を登録する（敵登録System が ForEach 内で呼ぶ）。
    void Insert(int cellI, int cellJ, const EnemyEntry& entry)
    {
        _buckets[Key(cellI, cellJ)].push_back(entry);
    }

    /**
     * @brief 指定セルとその隣接8セルの全エントリを取得する（弾が自セル＋近傍を見るため）。
     * @param outEntries 結果を追記するバッファ（毎回 clear してから渡すこと）。
     */
    void QueryNeighbors(int cellI, int cellJ, std::vector<EnemyEntry>& outEntries) const
    {
        for (int di = -1; di <= 1; ++di)
            for (int dj = -1; dj <= 1; ++dj)
            {
                auto it = _buckets.find(Key(cellI + di, cellJ + dj));
                if (it == _buckets.end()) continue;
                for (const auto& e : it->second) outEntries.push_back(e);
            }
    }

private:
    static uint64_t Key(int i, int j)
    {
        // 負のセル座標も扱えるよう符号拡張してビット結合
        return (static_cast<uint64_t>(static_cast<uint32_t>(i)) << 32)
            | static_cast<uint64_t>(static_cast<uint32_t>(j));
    }

    std::unordered_map<uint64_t, std::vector<EnemyEntry>> _buckets;
};