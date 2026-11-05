#pragma once
#include "Engine/ECS/Common/CCL_Types.h"

namespace CCL::Serialization
{

    // 保存できる Entity への参照。保存時はローカルインデックスへ、読込時は新しい EntityID へ自動で直る
    // 比較は暗黙変換 operator EntityID() に任せるので、link == 0 も link == someID も EntityID 同士の比較になる
    struct EntityLink
    {
        CCL::ECS::EntityID id = CCL::ECS::InvalidEntityID;

        constexpr EntityLink() = default;
        constexpr EntityLink(CCL::ECS::EntityID e) : id(e) {}

        // EntityID への暗黙変換。operator== / operator!= は意図して持たない
        //   メンバーの operator== と暗黙変換が両方あると、link1 == link2 で
        //     A: link1.operator==(link2)
        //     B: (EntityID)link1 == (EntityID)link2
        //   が同じコストで候補になり、C2593（あいまいな変換）になる
        //   メンバーの == がなければ B だけになり、一意に決まる
        constexpr operator CCL::ECS::EntityID() const { return id; }

        EntityLink &operator=(CCL::ECS::EntityID e)
        {
            id = e;
            return *this;
        }

        // 0 と InvalidEntityID はどちらも「参照なし」として扱う（TransformComponent.parent は 0 がルート）
        bool IsValid() const
        {
            return id != CCL::ECS::InvalidEntityID && id != 0;
        }

        void Reset()
        {
            id = CCL::ECS::InvalidEntityID;
        }
    };

    // offset で EntityID と同じように読み書きするので、サイズが同じであること
    static_assert(sizeof(EntityLink) == sizeof(CCL::ECS::EntityID),
                  "EntityLink must be same size as EntityID.");

} // namespace CCL::Serialization