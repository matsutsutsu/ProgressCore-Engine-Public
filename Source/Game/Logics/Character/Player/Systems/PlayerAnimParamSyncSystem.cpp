/// @file PlayerAnimParamSyncSystem.cpp

#include "PlayerAnimParamSyncSystem.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// 毎フレームの HashString 計算を省くための事前計算（キャッシュ）
static const uint32_t HASH_IS_DASHING = CCL::Utils::HashString("IsDashing");

void PlayerAnimParamSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const DashComponent &dash, AnimParametersComponent &animParams) {
        animParams.SetBool(HASH_IS_DASHING, dash.isDashing);
    });
}

// 掲示板を消費する AnimControllerSystem(L02_Update=200) より必ず前に走らせる。
REGISTER_LOGIC_SYSTEM(PlayerAnimParamSyncSystem, Priority::LogicStage::L02_PreUpdate);
