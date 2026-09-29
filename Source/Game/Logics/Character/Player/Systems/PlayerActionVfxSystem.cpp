#include "PlayerActionVfxSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/Systems/Particle/VFXSystem.h"
#include "Game/Logics/GameEvent/GameEvents.h"

#include "Engine/Graphics/Components/Trail/TrailComponent.h"

using namespace CCL::ECS;

namespace
{
    // バーストエミッタのプレハブ（拡張子 .json は Prefab 側が付ける）。見た目はエディタで調整する。
    const char *kJumpVfxPrefab = "Assets/Prefabs/Particles/VFX_Jump";
    const char *kDashVfxPrefab = "Assets/Prefabs/Particles/VFX_Dash";
} // namespace

void PlayerActionVfxSystem::Initialize()
{
    // 2段ジャンプ（jumpCount>=2）の瞬間だけ、その場に土煙の VFX を出す。
    //   地上からの1段目では出さず、空中でもう一度跳んだことを目立たせる。
    ListenEvent<PlayerJumpedEvent>([this](const PlayerJumpedEvent &e) {
        if (e.jumpCount >= 2)
            VFX::Play(*m_world, kJumpVfxPrefab, e.position);
    });

    // ダッシュ（回避）開始の瞬間に、その場に VFX を出し、足元のトレイルを出す。
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &e) {
        float             vfxYOffset  = 2.0f;
        DirectX::XMFLOAT3 vfxPosition = {e.position.x, e.position.y + vfxYOffset, e.position.z};

        VFX::Play(*m_world, kDashVfxPrefab, vfxPosition);

        ActivatePlayerFootTrails(e.playerID);
    });
}

void PlayerActionVfxSystem::Update(float /*dt*/)
{
    // 毎フレームの処理は無い。再生中の VFX の片付けは VFXSystem が行う。
    //   イベントを購読するためだけに System にしている。
}

void PlayerActionVfxSystem::ActivatePlayerFootTrails(CCL::ECS::EntityID playerID)
{
    auto *playerTransform = m_world->GetComponent<TransformComponent>(playerID);
    if (!playerTransform) return;

    // 直接の子を兄弟リンク（firstChildID → nextSiblingID）でたどる。終端は 0（InvalidEntityID ではない）。
    EntityID childID = playerTransform->firstChildID;
    while (childID != 0)
    {
        if (auto *trail = m_world->GetComponent<TrailComponent>(childID))
        {
            trail->ActivateFor(trail->burstDuration);
        }

        auto *childTransform = m_world->GetComponent<TransformComponent>(childID);
        if (!childTransform) break;

        childID = childTransform->nextSiblingID;
    }
}

// 処理はイベントで行う。HitEffectSystem と同じ L07_Cleanup に置く。
REGISTER_LOGIC_SYSTEM(PlayerActionVfxSystem, Priority::LogicStage::L07_Cleanup);
