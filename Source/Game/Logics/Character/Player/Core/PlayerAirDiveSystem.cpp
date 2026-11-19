#include "PlayerAirDiveSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/StringHash.h"

#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // 空中ダイブのアニメのステート名のハッシュ（構え / 落下）
    const uint32_t HASH_STATE_DIVE_START = CCL::Utils::HashString("Air_Dive_Start");
    const uint32_t HASH_STATE_DIVE_LOOP  = CCL::Utils::HashString("Air_Dive_Loop");

    constexpr float kDiveFallSpeed    = 45.0f; // 落ちる速さ（m/秒）
    constexpr float kDiveForwardSpeed = 6.0f;  // 落ちながら前へ進む速さ（m/秒）
}



void PlayerAirDiveSystem::Update(float dt)
{
    ForEach([&](CharacterMovementComponent      &movement,
                const AnimStateMachineComponent &fsm,
                const PlayerComponent           &player) {
        // 構えでは重力を切って空中に止め、溜めてから落ちるように見せる
        // use3DVelocity は重力とジャンプより優先される1ステップ限りの依頼なので、この間は毎ステップ書く
        if (fsm.GetCurrentStateHash() == HASH_STATE_DIVE_START)
        {
            movement.use3DVelocity = true;
            movement.velocity3D    = Vector3::Zero;
            return;
        }

        // 落下は向いている方へ少し進めながら落とす。着地すればグラフが Air_Dive_End へ移り、ここで書かなくなる
        if (fsm.GetCurrentStateHash() == HASH_STATE_DIVE_LOOP)
        {
            const Vector3 forward(std::sin(player.currentYaw), 0.0f, std::cos(player.currentYaw));
            movement.use3DVelocity = true;
            movement.velocity3D    = forward * kDiveForwardSpeed + Vector3(0.0f, -kDiveFallSpeed, 0.0f);
        }
    });
}



// 物理（JoltCharacterUpdateSystem, L03_PrePhysics）より前に、そのステップの速度を書く
REGISTER_LOGIC_SYSTEM(PlayerAirDiveSystem, Priority::LogicStage::L02_Update);
