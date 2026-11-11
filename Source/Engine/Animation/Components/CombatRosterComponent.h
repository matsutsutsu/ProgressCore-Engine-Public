#pragma once
#include "Engine/ECS/Common/CCL_Types.h"
#include <cstring>

// キャラの武器や部位を、"RightHand" のようなタグから子エンティティへ引く表。キャラのルートに付ける
// チャンクに直接置けるよう、map でなく固定長の配列にした POD。VFXSystem が使うので Engine 側に置く
struct CombatRosterComponent
{
    static constexpr int kMaxEntries = 4; // 登録できる部位の数（増やすとコンポーネントが大きくなる）

    struct Entry
    {
        char               tag[32] = {};                        // 部位のタグ（"RightHand" など）
        CCL::ECS::EntityID id      = CCL::ECS::InvalidEntityID; // インスペクタで「なし」にすると 0 が入る
    };
    int   count = 0; // entries のうち使っている数
    Entry entries[kMaxEntries];

    // タグに対応する Entity を返す。見つからない・割り当てが「なし」なら InvalidEntityID
    // 「なし」の 0 もここで InvalidEntityID に直すので、呼ぶ側は InvalidEntityID とだけ比べればよい
    CCL::ECS::EntityID Find(const char *targetTag) const
    {
        if (!targetTag) return CCL::ECS::InvalidEntityID;
        for (int i = 0; i < count; ++i)
        {
            if (std::strcmp(entries[i].tag, targetTag) == 0)
                return (entries[i].id != 0) ? entries[i].id : CCL::ECS::InvalidEntityID;
        }
        return CCL::ECS::InvalidEntityID;
    }
};