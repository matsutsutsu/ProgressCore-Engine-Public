#pragma once
// ===========================================================================
// 概要: ムービーの対象（subject）を、Entity の名前で探す関数。
//
// 責務:
//   - 名前（NameComponent）が一致する最初の Entity を返す
//
// 備考:
//   カメラ（CinematicCameraSystem）とトラック（CinematicTrackSystem）が同じ探し方をするので、1か所に置く。
//   全 Entity の名前を比べるので、毎フレームではなく、ショットに入る・点を出す・区間に入る瞬間にだけ呼ぶ。
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Core/Components/NameComponent.h"

#include <string>

namespace Cinematic
{
    /**
     * @brief 名前（NameComponent）から Entity を探す。
     * @param world 探す World。
     * @param name  探す名前。
     * @return 最初に見つかった Entity。見つからなければ InvalidEntityID。
     */
    inline CCL::ECS::EntityID FindEntityByName(CCL::ECS::World &world, const std::string &name)
    {
        CCL::ECS::EntityID found = CCL::ECS::InvalidEntityID;

        world.ViewEach<NameComponent>([&](CCL::ECS::EntityID id) {
            // すでに見つかっていたら、残りの Entity は無視する
            if (found != CCL::ECS::InvalidEntityID) return;

            // 名前が一致する Entity を探す
            const auto *nameComp = world.GetComponent<NameComponent>(id);
            if (nameComp && name == nameComp->name) found = id;
        });
        return found;
    }
} // namespace Cinematic
