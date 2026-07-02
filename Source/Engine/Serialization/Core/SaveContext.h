// =====================================================================
// SaveContext.h
//   シーン保存(Serialize)中に生きる、EntityID → ローカルインデックスの辞書。
//
//   設計のキモ:
//     - ファイルに書き出されるのは raw EntityID ではなく「ローカルインデックス」
//       (0, 1, 2, ... の連番)
//     - 同じファイル内で発行された ID は一貫している
//     - 別ファイル/別シーンとマージしても衝突しない
//     - DCC ツール (Blender 等) から手書き JSON を流し込むのも容易
//
//   なぜ vector ではなく unordered_map を保存側に使うか:
//     - 保存側は「ある EntityID を見たことがあるか?」が頻繁なクエリ
//     - 連番割当は付加情報なので、ハッシュ引きが自然
//     - 復元側 (LoadContext) は配列インデックス引きなので vector で OK
//
//   将来拡張:
//     - 外部参照 (別シーンのエンティティ) を表す「特殊ローカルインデックス」を
//       予約済み範囲 (例: 0xFFFFFF00 以上) で扱える設計にしておく
// =====================================================================
#pragma once
#include <cstdint>
#include <unordered_map>
#include "Engine/ECS/Common/CCL_Types.h"

namespace CCL::Serialization {


    class SaveContext {
    public:
        // ローカルインデックスの型。32bit で十分 (1 シーン 40 億エンティティはまずない)。
        using LocalIndex = uint32_t;

        // 0 は「無効/未参照」を意味する予約値。実エンティティには 1 以降を割り当てる。
        // (JSON で 0 を見たら「参照なし」と即判断できるよう、設計上のルールとする)
        static constexpr LocalIndex InvalidLocalIndex = 0;

        // EntityID を渡し、まだ登録されていなければ新しいローカルインデックスを発行する。
        // 既に登録済みならその値を返す。常に同じ EntityID には同じインデックスが返る。
        LocalIndex GetOrRegister(CCL::ECS::EntityID realID)
        {
            // 無効 ID は即 0 を返す (ファイル上の「参照なし」表現)
            if (realID == CCL::ECS::InvalidEntityID) return InvalidLocalIndex;

            // ※ EntityID == 0 を「無効」と扱うか「有効な ID #0」と扱うかは
            //   エンジン仕様による。今回のコードでは ID=0 を実エンティティに割り当てている
            //   ようなので、0 を InvalidEntityID と混同しないようガード。
            //   元コードに「if (realID == 0) return 0」があったが、これだと
            //   先頭エンティティの参照を全部潰してしまうため、ここでは行わない。

            auto it = _idToLocal.find(realID);
            if (it != _idToLocal.end()) return it->second;

            LocalIndex newIdx = _nextLocalIndex++;
            _idToLocal.emplace(realID, newIdx);
            return newIdx;
        }

        // 純粋なルックアップ (副作用なし)。未登録なら 0 を返す。
        // ヘッダ書き出し時など、登録済みであるべき場面で使う。
        LocalIndex Lookup(CCL::ECS::EntityID realID) const
        {
            auto it = _idToLocal.find(realID);
            return it != _idToLocal.end() ? it->second : InvalidLocalIndex;
        }

        // 登録されているエンティティ数
        size_t Size() const { return _idToLocal.size(); }

        void Clear()
        {
            _idToLocal.clear();
            _nextLocalIndex = 1; // 0 は「無効」予約
        }

    private:
        std::unordered_map<CCL::ECS::EntityID, LocalIndex> _idToLocal;
        LocalIndex _nextLocalIndex = 1; // 1 から発行 (0 は無効)
    };

} // namespace CCL::Serialization