#pragma once
// 武器や判定などの子孫から、アニメーションの持ち主（Animator を持つ祖先）を引く共通関数
// 区間イベントで動く System（攻撃判定・トレイル・パーティクル）が使う
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Animation/Components/AnimatorComponent.h"

namespace CCL::Anim
{
    // self の親から順にたどり、最初に Animator を持つ Entity を返す（self は調べない）。なければ InvalidEntityID
    // 親の循環に備えて 32 段で打ち切る
    inline CCL::ECS::EntityID FindAnimatorAncestor(CCL::ECS::World &world, const TransformComponent &self)
    {
        constexpr int kMaxDepth = 32;

        CCL::ECS::EntityID cur = self.parent; // EntityLink から EntityID へ暗黙に変換される
        for (int depth = 0; depth < kMaxDepth; ++depth)
        {
            // インスペクタで親を「なし」にすると 0 が入る
            if (cur == CCL::ECS::InvalidEntityID || cur == 0) return CCL::ECS::InvalidEntityID;
            if (world.GetComponent<AnimatorComponent>(cur)) return cur;

            const auto *parent = world.GetComponent<TransformComponent>(cur);
            cur                = parent ? static_cast<CCL::ECS::EntityID>(parent->parent) : CCL::ECS::InvalidEntityID;
        }
        return CCL::ECS::InvalidEntityID;
    }



    // cachedOwner がまだ Animator を持っていればそのまま返し、持っていなければ親をたどって探し直す
    // 見つからない間は呼ぶたびにたどるので、階層が後から組まれても追従する
    inline CCL::ECS::EntityID RefreshAnimatorOwner(CCL::ECS::World &world, const TransformComponent &self,
                                                   CCL::ECS::EntityID cachedOwner)
    {
        if (world.GetComponent<AnimatorComponent>(cachedOwner)) return cachedOwner;
        return FindAnimatorAncestor(world, self);
    }
}
