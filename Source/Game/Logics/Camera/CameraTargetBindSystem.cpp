#include "Game/Logics/Camera/CameraTargetBindSystem.h"

#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"

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



void CameraTargetBindSystem::BindIfInvalid(CCL::Serialization::EntityLink &target, EntityID entity)
{
    if (entity == InvalidEntityID) return;
    if (!m_world->IsEntityValid(target.id)) target.id = entity;
}



void CameraTargetBindSystem::Update(float dt)
{
    const EntityID playerId = FindPlayer();
    if (playerId == InvalidEntityID) return;

    // VirtualCamera を持つエンティティを走査し、付いているカメラ Component の種類ごとに target を繋ぎ直す
    ForEachWithID([&](EntityID camID, const VirtualCamera & /*vcam*/) {
        if (auto *follow = m_world->GetComponent<CameraBodyFollow>(camID))
            BindIfInvalid(follow->target, playerId);

        if (auto *aim = m_world->GetComponent<CameraAimLookAt>(camID))
            BindIfInvalid(aim->target, playerId);

        if (auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID))
            BindIfInvalid(tps->target, playerId);
    });
}



// カメラ本体(R01_Camera=100)より必ず前に走らせる
REGISTER_RENDER_SYSTEM(CameraTargetBindSystem, Priority::RenderStage::R00b_CameraTargetBind);
