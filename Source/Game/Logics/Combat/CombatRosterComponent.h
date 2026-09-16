#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include <cstring> // for strcmp

// 武器や部位のIDを即座に引くための辞書。
//   キャラのルートに持たせ、"RightHand" のようなタグから対応する子エンティティを引く。
//   std::map ではなく固定長配列にしてPOD化し、チャンクへ直接載せられるようにしている。
//   AnimationSequencerWindow は、この tag 一覧を通知イベントの武器指定プルダウンに使う。
struct CombatRosterComponent
{
    // 同時に登録できる部位の数。増やすとコンポーネントのサイズが直接増える。
    static constexpr int kMaxEntries = 4;

    struct Entry
    {
        char               tag[32] = {}; // "RightHand", "LeftFoot" など
        CCL::ECS::EntityID id      = CCL::ECS::InvalidEntityID;
    };
    int   count = 0;
    Entry entries[kMaxEntries];

    // タグに対応するエンティティを返す。見つからなければ InvalidEntityID。
    //
    // 「無し」を 0 で返してはいけない。このエンジンは ID=0 を実エンティティに
    // 割り当てるため、0 を返すと無関係な先頭エンティティを指してしまう。
    CCL::ECS::EntityID Find(const char *targetTag) const
    {
        if (!targetTag) return CCL::ECS::InvalidEntityID;
        for (int i = 0; i < count; ++i)
        {
            if (std::strcmp(entries[i].tag, targetTag) == 0) return entries[i].id;
        }
        return CCL::ECS::InvalidEntityID;
    }
};