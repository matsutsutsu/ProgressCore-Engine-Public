#include "Game/Logics/Camera/PlayerCameraLookup.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Camera/PlayerCameraComponent.h"

using namespace CCL::ECS;

// playerID を追っている TPS カメラを、最初に見つかった1台だけ返す
EntityID FindPlayerCamera(World &world, EntityID playerID)
{
    if (playerID == InvalidEntityID) return InvalidEntityID;

    EntityID found = InvalidEntityID;
    world.ViewEach<PlayerCameraComponent>([&](EntityID camID) {
        if (found != InvalidEntityID) return;

        const auto *tps = world.GetComponent<PlayerCameraComponent>(camID);
        if (tps && tps->target.id == playerID) found = camID;
    });
    return found;
}
