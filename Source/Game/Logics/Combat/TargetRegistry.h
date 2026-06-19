#pragma once
// ===========================================================================
// TargetRegistry.h
// ----------------
// 役割:
//   毎フレーム構築される「狙える対象(敵)の位置リスト」。Homing弾の追尾先探索や、
//   将来の敵AIのターゲット選択が共有して参照する World Resource。
//
// 責務:
//   - (EntityID, position) の平坦な配列の保持と、最寄り探索ヘルパの提供のみ。
//
// アーキテクチャ上の位置付け:
//   TargetRegistrySystem(L02_PreUpdate) が毎フレーム先頭でクリア＆再構築する。
//   以降の L02_Update で BulletTravelSystem(Homing) 等が読み取り専用で参照する。
//
// 設計方針:
//   - 敵のTransformランダムアクセスを「1フレーム1回・1箇所」に集約する。各弾が個別に
//     GetComponent<Transform> するより、平坦配列を線形走査する方がキャッシュに優しい。
//   - まずは線形探索(O(対象数))。敵が数百規模になったら空間分割(グリッド)へ差し替える
//     余地を残す(本クラスのAPIを保てば呼び出し側は無変更で済む)。
//   - 状態は配列のみ。System が構築し、他は読むだけ(SSOTを侵さない)。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include "SimpleMath.h"
#include <vector>

/// @brief 狙える対象の (ID, 位置) を保持し、最寄り探索を提供する World Resource。
class TargetRegistry
{
public:
    struct Entry
    {
        CCL::ECS::EntityID id;
        DirectX::SimpleMath::Vector3 position;
    };

    /// @brief 構築開始時にクリアする（毎フレーム先頭で呼ぶ）。
    void Clear() { _entries.clear(); }

    /// @brief 対象を追加する（構築System が敵を1体ずつ登録）。
    void Add(CCL::ECS::EntityID id, const DirectX::SimpleMath::Vector3& pos)
    {
        _entries.push_back({ id, pos });
    }

    /// @brief 登録対象が1つでもあるか。
    bool Empty() const { return _entries.empty(); }

    size_t Count() const { return _entries.size(); }

    const std::vector<Entry>& Entries() const { return _entries; }

    /**
     * @brief from に最も近い対象を探す（XZ平面距離）。
     * @param from     探索の基準位置
     * @param exclude  除外するID（自分自身など。不要なら InvalidEntityID）
     * @param outPos   見つかった対象の位置を受け取る
     * @return 見つかった対象のID。無ければ InvalidEntityID。
     */
    CCL::ECS::EntityID FindNearest(const DirectX::SimpleMath::Vector3& from,
        CCL::ECS::EntityID exclude,
        DirectX::SimpleMath::Vector3& outPos) const
    {
        CCL::ECS::EntityID best = CCL::ECS::InvalidEntityID;
        float bestDistSq = 3.4e38f; // FLT_MAX 相当

        for (const Entry& e : _entries)
        {
            if (e.id == exclude) continue;

            const float dx = e.position.x - from.x;
            const float dz = e.position.z - from.z; // 平面ステージ＝XZ距離
            const float distSq = dx * dx + dz * dz;

            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                best = e.id;
                outPos = e.position;
            }
        }
        return best;
    }

private:
    std::vector<Entry> _entries; ///< 毎フレーム再構築される対象リスト
};