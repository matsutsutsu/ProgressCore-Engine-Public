/// @file PlayerActionVfxSystem.cpp
/// 役割・設計方針は PlayerActionVfxSystem.h を参照。
#include "PlayerActionVfxSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Systems/Particle/VFXSystem.h"
#include "Game/Logics/GameEvent/GameEvents.h"

using namespace CCL::ECS;

namespace
{
// バーストエミッタ・プレハブ（.json は Prefab 側が補完する）。見た目はエディタで調整する。
const char *kJumpVfxPrefab = "Assets/Prefabs/Particles/VFX_Jump";
const char *kDashVfxPrefab = "Assets/Prefabs/Particles/VFX_Dash";
} // namespace

void PlayerActionVfxSystem::Initialize()
{
    // 2段ジャンプ（jumpCount>=2）の瞬間だけ、その場に土煙VFXを出す。
    //   1段目（地上ジャンプ）では出さない＝空中で「もう一度蹴った」感触を強調する。
    ListenEvent<PlayerJumpedEvent>([this](const PlayerJumpedEvent &e) {
        if (e.jumpCount >= 2)
            VFX::Play(*m_world, kJumpVfxPrefab, e.position);
    });

    // ダッシュ（回避バースト）開始の瞬間に、その場にVFXを出す。
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &e) {
        VFX::Play(*m_world, kDashVfxPrefab, e.position);
    });
}

void PlayerActionVfxSystem::Update(float /*dt*/)
{
    // 何もしない。再生中インスタンスの後始末は VFXSystem(L08) が持つ。
    //   購読のためだけに System として存在している。
}

// 実処理はイベント駆動。片付け相当のタイミングに置く（HitEffectSystem と同じ L07）。
REGISTER_LOGIC_SYSTEM(PlayerActionVfxSystem, Priority::LogicStage::L07_Cleanup);
