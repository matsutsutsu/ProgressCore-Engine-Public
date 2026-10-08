#pragma once
// ===========================================================================
// 概要: 読込中に使う「ローカルインデックス → 新しい EntityID」の対応表 LoadContext。
//
// 責務:
//   - Entity を生成したときに「ローカルインデックス → 新しい EntityID」を登録する
//   - コンポーネントの復元時に、EntityLink のローカルインデックスを EntityID に変換する
//
// 備考:
//   - ローカルインデックスは 1 からの連番なので、map ではなく vector の添字で引く。
//   - 0（参照なし）と範囲外は InvalidEntityID を返す（例外は投げない）。
// ===========================================================================
#include <cstdint>
#include <vector>
#include "Engine/ECS/Common/CCL_Types.h"
#include "SaveContext.h" // LocalIndex の定義を共有

namespace CCL::Serialization
{

    class LoadContext
    {
    public:
        using LocalIndex = SaveContext::LocalIndex;

        // 復元する Entity の数が分かっているときに呼ぶ（vector の再確保を防ぐ）
        void Reserve(size_t count)
        {
            // インデックスは 1 から始まるので +1
            m_localToEntity.reserve(count + 1);
            EnsureSize(count + 1);
        }

        // ローカルインデックスに対応する新しい EntityID を登録する（Entity の生成時に呼ぶ）
        void Register(LocalIndex localIdx, CCL::ECS::EntityID realID)
        {
            if (localIdx == SaveContext::InvalidLocalIndex) return;
            EnsureSize(localIdx + 1);
            m_localToEntity[localIdx] = realID;
        }

        // ローカルインデックスを EntityID に変換する。0 や範囲外なら InvalidEntityID
        CCL::ECS::EntityID Resolve(LocalIndex localIdx) const
        {
            if (localIdx == SaveContext::InvalidLocalIndex) return CCL::ECS::InvalidEntityID;
            if (localIdx >= m_localToEntity.size()) return CCL::ECS::InvalidEntityID;
            return m_localToEntity[localIdx];
        }

        void Clear()
        {
            m_localToEntity.clear();
        }

    private:
        // [0] は「参照なし」用に InvalidEntityID を入れておく
        std::vector<CCL::ECS::EntityID> m_localToEntity{CCL::ECS::InvalidEntityID};

        void EnsureSize(size_t needed)
        {
            if (m_localToEntity.size() < needed)
            {
                m_localToEntity.resize(needed, CCL::ECS::InvalidEntityID);
            }
        }
    };

} // namespace CCL::Serialization