
#include "CameraTargetBindSystem.h"

#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/Camera/Components/CameraBodyIntroOrbitComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"

using namespace CCL::ECS;

EntityID CameraTargetBindSystem::FindPlayer()
{
    if (!m_world) return InvalidEntityID;
    for (auto &chunkPtr : m_world->GetChunkManager().GetChunks())
    {
        if (!chunkPtr) continue;
        Chunk *chunk = chunkPtr.get();
        if (!chunk->HasComponent<PlayerComponent>()) continue;
        for (size_t i = 0; i < chunk->GetEntityCount(); ++i)
        {
            if (chunk->IsEntityDestroyed(i)) continue;
            return chunk->GetEntityByIndex(i);
        }
    }
    return InvalidEntityID;
}

EntityID CameraTargetBindSystem::FindBoss()
{
    if (!m_world) return InvalidEntityID;
    for (auto &chunkPtr : m_world->GetChunkManager().GetChunks())
    {
        if (!chunkPtr) continue;
        Chunk *chunk = chunkPtr.get();
        if (!chunk->HasComponent<BossPhaseComponent>()) continue;
        for (size_t i = 0; i < chunk->GetEntityCount(); ++i)
        {
            if (chunk->IsEntityDestroyed(i)) continue;
            return chunk->GetEntityByIndex(i);
        }
    }
    return InvalidEntityID;
}

void CameraTargetBindSystem::BindIfInvalid(CCL::Serialization::EntityLink &target, EntityID player)
{
    if (player == InvalidEntityID) return;
    if (!m_world->IsEntityValid(target.id)) target.id = player;
}

void CameraTargetBindSystem::Update(float /*dt*/)
{
    const EntityID playerId = FindPlayer();
    const EntityID bossId   = FindBoss(); // 居ないシーンでは Invalid のまま（演出側が打ち切る）
    if (playerId == InvalidEntityID && bossId == InvalidEntityID) return;

    // カメラ本体(VirtualCamera)を持つエンティティを走査し、
    // 載っている「体(Body)」の種類ごとに target を繋ぎ直す。
    ForEachWithID([&](EntityID camID, const VirtualCamera & /*vcam*/) {
        if (auto *follow = m_world->GetComponent<CameraBodyFollow>(camID))
            BindIfInvalid(follow->target, playerId);

        if (auto *aim = m_world->GetComponent<CameraAimLookAt>(camID))
            BindIfInvalid(aim->target, playerId);

        if (auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID))
            BindIfInvalid(tps->target, playerId);

        // 導入演出カメラだけはプレイヤーではなくボスを見る。
        // これでプレハブを置くだけで演出が成立し、EntityID の手動設定が要らない。
        if (auto *intro = m_world->GetComponent<CameraBodyIntroOrbit>(camID))
            BindIfInvalid(intro->target, bossId);
    });
}

// カメラ本体(R01_Camera=100)より必ず前に走らせる。
REGISTER_RENDER_SYSTEM(CameraTargetBindSystem, Priority::RenderStage::R00b_CameraTargetBind);
