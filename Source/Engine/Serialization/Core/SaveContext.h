#pragma once
// ===========================================================================
// 概要: 保存中に使う「EntityID → ローカルインデックス」の対応表 SaveContext
//
// 責務:
//   - 保存する Entity に 1 からの連番（ローカルインデックス）を割り当てる
//   - EntityLink を書き出すときに、参照先の EntityID をローカルインデックスに変換する
//
// 備考: ファイルには実行ごとに変わる EntityID でなく、ファイルの中で閉じたローカルインデックスを書く
//       EntityID は連番でないので、引くのは unordered_map（読む側の LoadContext は連番なので vector）
// ===========================================================================
#include <cstdint>
#include <unordered_map>
#include "Engine/ECS/Common/CCL_Types.h"

namespace CCL::Serialization
{

    class SaveContext
    {
    public:
        // ローカルインデックスの型（1 ファイルの Entity 数には 32bit で十分）
        using LocalIndex = uint32_t;

        // 0 は「参照なし」を表す予約値。実際の Entity には 1 以降を割り当てる
        static constexpr LocalIndex InvalidLocalIndex = 0;

        // EntityID のローカルインデックスを返す。未登録なら新しく割り当てる
        LocalIndex GetOrRegister(CCL::ECS::EntityID realID)
        {
            // 0 と InvalidEntityID は「参照なし」（EntityLink::IsValid と同じ判定）
            //   EntityID は上位 32bit に 1 から始まる世代を持つので、実在する Entity が 0 になることはない
            if (realID == CCL::ECS::InvalidEntityID || realID == 0) return InvalidLocalIndex;

            auto it = m_idToLocal.find(realID);
            if (it != m_idToLocal.end()) return it->second;

            LocalIndex newIdx = m_nextLocalIndex++;
            m_idToLocal.emplace(realID, newIdx);
            return newIdx;
        }

        // 登録せずに引く。未登録なら 0 を返す
        LocalIndex Lookup(CCL::ECS::EntityID realID) const
        {
            auto it = m_idToLocal.find(realID);
            return it != m_idToLocal.end() ? it->second : InvalidLocalIndex;
        }

        // 登録されている Entity の数
        size_t Size() const { return m_idToLocal.size(); }

        void Clear()
        {
            m_idToLocal.clear();
            m_nextLocalIndex = 1;
        }

    private:
        std::unordered_map<CCL::ECS::EntityID, LocalIndex> m_idToLocal;
        LocalIndex                                         m_nextLocalIndex = 1; // 次に割り当てる番号（0 は予約）
    };

}