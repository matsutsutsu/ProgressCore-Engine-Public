#pragma once
// ===========================================================================
// Registry/MagicBulletPool.h
// --------------------------
// @file
// @brief 弾の同時存在数に上限(既定8192)を課すグローバル統治者(World Resource)。
//        事前確保はせず、ECS本来の spawn/destroy に「上限＋最古リサイクル」を被せる。
//
// 使い方:
//   発射前  : TryReserve(evict) → evict が有効なら最古弾を Destroy → spawn → Register
//   弾死亡時: Unregister(id)   (lifetime / collision から)
//
// @warning 書き換えは fire(L02)/collision(L06)/inheritance(L07) からのみ。いずれも直列実行で、
//          並列System(BulletTravel)は触らないため競合しない。
// ===========================================================================

#include <deque>
#include <unordered_set>
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Engine/ECS/Common/CCL_Types.h"

/**
 * @brief 弾の同時数を上限以下に保つガバナ。満杯時は最古の生存弾を退役させる。
 */
class MagicBulletPool
{
public:
    explicit MagicBulletPool(uint32_t capacity = 8192) : _capacity(capacity) {}

    /**
     * @brief 発射枠を予約する。満杯なら最古の生存弾を退役対象として返す。
     * @param outEvict 退役させるべき最古弾ID（不要なら InvalidEntityID）。
     * @return 常に true（リサイクルで必ず枠を空ける方針）。drop方針にしたいならここを false 返しに変える。
     */
    bool TryReserve(CCL::ECS::EntityID& outEvict)
    {
        outEvict = CCL::ECS::InvalidEntityID;
        if (_live.size() < _capacity) return true;

        // 満杯：FIFO先頭から「まだ生きている最古弾」を見つけて退役。死亡済みエントリは遅延削除。
        while (!_fifo.empty())
        {
            const CCL::ECS::EntityID oldest = _fifo.front();
            _fifo.pop_front();
            auto it = _live.find(oldest);
            if (it != _live.end())
            {
                _live.erase(it);
                outEvict = oldest;
                return true;
            }
        }
        return true;
    }

    /// @brief 生成確定した弾を登録（spawn順 = FIFO）。
    void Register(CCL::ECS::EntityID id)
    {
        _live.insert(id);
        _fifo.push_back(id);
    }

    /// @brief 死亡した弾を解除。FIFO からは遅延削除（TryReserve 時に掃除）。
    void Unregister(CCL::ECS::EntityID id) { _live.erase(id); }

    uint32_t LiveCount() const { return static_cast<uint32_t>(_live.size()); }
    uint32_t Capacity()  const { return _capacity; }

private:
    uint32_t _capacity;
    std::unordered_set<CCL::ECS::EntityID> _live; ///< 生存弾集合。size = 同時存在数。
    std::deque<CCL::ECS::EntityID>         _fifo; ///< spawn順。死亡分は遅延削除。
};