#include "ChargeMotionRegistry.h"

#include <iterator>

#include "Game/Logics/AI/Drone/Motion/RigidFollowMotion.h"
#include "Game/Logics/AI/Drone/Motion/SoftFollowMotion.h"
#include "Game/Logics/AI/Drone/Motion/StraightChargeMotion.h"

namespace CCL::AI::Motion
{
    namespace
    {
        // 動き方は1種類につき1つを全機で共有する。
        // const にしないのは BeginFrame で調整値を写し取るため（Move の間は読むだけ）。
        StraightChargeMotion gStraight{};
        RigidFollowMotion    gRigidFollow{};
        SoftFollowMotion     gSoftFollow{};

        // 並びは ChargeMode の宣言順（添字がそのまま値になる）。
        IChargeMotion *const kTable[] = {&gStraight, &gRigidFollow, &gSoftFollow};
    } // namespace

    const IChargeMotion &GetChargeMotion(ChargeMode mode)
    {
        const size_t index = static_cast<size_t>(mode);
        return (index < std::size(kTable)) ? *kTable[index] : gStraight;
    }

    void BeginChargeMotionsFrame(CCL::ECS::World &world)
    {
        for (IChargeMotion *motion : kTable)
            motion->BeginFrame(world);
    }

} // namespace CCL::AI::Motion
