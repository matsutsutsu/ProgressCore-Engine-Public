#pragma once
// ===========================================================================
// ParticlePool.h
// --------------
// 役割:
//   poolKey(= パーティクルPrefabのJSONパス)ごとに、休眠中の emitterRoot
//   (エミッタ階層の根Entity) を貯めて使い回すための空きリスト。
//
// 責務:
//   - emitterRoot の EntityID 群の貸し出し(Acquire)・返却(Release)の記録のみ。
//   - 階層の起こし/休眠やECS操作そのものは持たない(それは Prefab 側の責務)。
//
// アーキテクチャ上の位置付け:
//   World Resource として常駐し、Prefab::SpawnParticle / ReturnParticleToPool が
//   _world->GetResource<ParticlePool>() で取得して使う。
//
// 設計方針(重要):
//   - Prefab.cpp 内の static にせず、あえて World Resource として持つ。これにより
//     World の寿命にプールが完全に従属する。デバイスロスト等で ECS World をリセット
//     しても、古い(無効な)EntityID がプールに残り続ける破綻が構造的に起きない。
//   - 保持するのは ID のみ。Entity 実体の生成/破棄には関与しない(SSOTを侵さない)。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h" // CCL::ECS::EntityID / InvalidEntityID
#include <string>
#include <unordered_map>
#include <vector>

/// @brief poolKey ごとに休眠インスタンス(emitterRootのID)を貯める空きリスト。World Resource。
class ParticlePool
{
public:
    /// @brief key に対応する休眠インスタンスを1つ取り出す。
    /// @return 取り出した emitterRoot の ID。空きが無ければ InvalidEntityID。
    CCL::ECS::EntityID Acquire(const std::string& key)
    {
        auto it = _freeByKey.find(key);
        if (it == _freeByKey.end() || it->second.empty())
            return CCL::ECS::InvalidEntityID;
        CCL::ECS::EntityID id = it->second.back();
        it->second.pop_back();
        return id;
    }

    /// @brief 使い終わった emitterRoot を休眠リストへ戻す(破棄はしない)。
    void Release(const std::string& key, CCL::ECS::EntityID rootId)
    {
        if (rootId == CCL::ECS::InvalidEntityID) return;
        _freeByKey[key].push_back(rootId);
    }

    /// @brief 全空きリストを空にする。EntityID実体の破棄は呼び出し側の責務。
    void Clear() { _freeByKey.clear(); }

    /// @brief key の現在の空き数(デバッグ・プリウォーム判定用)。
    size_t FreeCount(const std::string& key) const
    {
        auto it = _freeByKey.find(key);
        return (it == _freeByKey.end()) ? 0 : it->second.size();
    }

private:
    std::unordered_map<std::string, std::vector<CCL::ECS::EntityID>> _freeByKey;
};