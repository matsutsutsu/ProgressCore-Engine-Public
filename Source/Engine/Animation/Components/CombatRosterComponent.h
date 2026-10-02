#pragma once
// ===========================================================================
// 概要: キャラの武器や部位を、"RightHand" のようなタグから子エンティティへ引く表。
//
// 責務:
//   - タグと子エンティティの組を最大 kMaxEntries 個持つ
//   - タグから子エンティティを探す（Find）
//
// 備考: キャラのルートに付ける。std::map ではなく固定長の配列にして POD にし、チャンクに直接置けるようにする。
//       AnimationSequencerWindow は、このタグの一覧を HitBox 区間の武器の選択肢に使う。
//       VFXSystem は、Effect イベントの基準にする武器をこのタグで引く（そのため Engine 側に置く）。
//       シリアライズとインスペクタの定義は Game 側の CombatComponentsMeta.cpp にある。
// ===========================================================================
#include "Engine/ECS/Common/CCL_Types.h"
#include <cstring>

struct CombatRosterComponent
{
    // 登録できる部位の数。増やすとそのぶんコンポーネントが大きくなる
    static constexpr int kMaxEntries = 4;

    struct Entry
    {
        char               tag[32] = {}; // "RightHand", "LeftFoot" など
        CCL::ECS::EntityID id      = CCL::ECS::InvalidEntityID; // インスペクタで「なし」にすると 0 が入る
    };
    int   count = 0;
    Entry entries[kMaxEntries];

    // タグに対応するエンティティを返す。見つからないとき・割り当てが「なし」のときは InvalidEntityID。
    // 呼び出し側が InvalidEntityID とだけ比べれば済むよう、「なし」の 0 もここで InvalidEntityID にする
    // （ID の世代は 1 から始まるので、0 は実在するエンティティを指さない）
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