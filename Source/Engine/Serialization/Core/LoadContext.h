// =====================================================================
// LoadContext.h
//   シーン復元(Deserialize)中に生きる、ローカルインデックス → EntityID の辞書。
//
//   旧 SerializationContext からの変更点:
//     - unordered_map<uint64_t, EntityID> → vector<EntityID> に変更
//       (ローカルインデックスは 0,1,2,... の連番なので配列引きで O(1))
//     - ハッシュ計算もアロケーションも発生しない
//     - 1000 エンティティのシーンで体感できるレベルの速度差が出る
//
//   役割:
//     - Pass 1: エンティティ生成時に「localIndex → 新 EntityID」を登録
//     - Pass 2: コンポーネント復元時に「localIndex → 実 EntityID」を解決
//     - Pass 3: 親子関係やリンクの最終確定
//
//   設計ルール:
//     - LocalIndex 0 は「参照なし」を意味する (Resolve(0) は InvalidEntityID)
//     - 範囲外アクセスは InvalidEntityID を返す (例外を投げない)
// =====================================================================
#pragma once
#include <cstdint>
#include <vector>
#include "Engine/ECS/Common/CCL_Types.h"
#include "SaveContext.h"  // LocalIndex の定義を共有

namespace CCL::Serialization {

    class LoadContext {
    public:
        using LocalIndex = SaveContext::LocalIndex;

        // 復元するエンティティ総数が分かっている時に呼ぶ (vector の再確保を防ぐ)
        void Reserve(size_t count)
        {
            // インデックス 1 始まりなので +1
            m_localToEntity.reserve(count + 1);
            EnsureSize(count + 1);
        }

        // ローカルインデックス N に対応する実 EntityID を登録する。
        // Pass 1 (エンティティ生成) で呼ばれる。
        void Register(LocalIndex localIdx, CCL::ECS::EntityID realID)
        {
            if (localIdx == SaveContext::InvalidLocalIndex) return;
            EnsureSize(localIdx + 1);
            m_localToEntity[localIdx] = realID;
        }

        // ローカルインデックスを実 EntityID に解決する。
        // 0 や範囲外なら InvalidEntityID。
        CCL::ECS::EntityID Resolve(LocalIndex localIdx) const
        {
            if (localIdx == SaveContext::InvalidLocalIndex) return CCL::ECS::InvalidEntityID;
            if (localIdx >= m_localToEntity.size())          return CCL::ECS::InvalidEntityID;
            return m_localToEntity[localIdx];
        }

        void Clear()
        {
            m_localToEntity.clear();
        }

    private:
        // [0] は「無効」用にダミー値 (InvalidEntityID) で確保しておく
        std::vector<CCL::ECS::EntityID> m_localToEntity{ CCL::ECS::InvalidEntityID };

        void EnsureSize(size_t needed)
        {
            if (m_localToEntity.size() < needed) {
                m_localToEntity.resize(needed, CCL::ECS::InvalidEntityID);
            }
        }
    };

} // namespace CCL::Serialization