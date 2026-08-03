/// @file CameraTargetBindSystem.cpp

#include "CameraTargetBindSystem.h"

#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

using namespace CCL::ECS;

namespace
{
    /// @brief PlayerComponent を持つ最初のエンティティ(=プレイヤー)を役割で探す。
    EntityID FindPlayer(World *world)
    {
        if (!world) return InvalidEntityID;
        for (auto &chunkPtr : world->GetChunkManager().GetChunks())
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

    /// @brief target が無効(未接続 / リロードでID変化 / リスポーン)なら player へ繋ぎ直す。
    ///        有効な相手を指している場合(手動設定など)は尊重して上書きしない。
    void BindIfInvalid(World *world, CCL::Serialization::EntityLink &target, EntityID player)
    {
        if (player == InvalidEntityID) return;
        if (!world->IsEntityValid(target.id)) target.id = player;
    }
} // namespace

void CameraTargetBindSystem::Update(float /*dt*/)
{
    const EntityID playerId = FindPlayer(m_world);
    if (playerId == InvalidEntityID) return;

    // カメラ本体(VirtualCamera)を持つエンティティを走査し、
    // 載っている「体(Body)」の種類ごとに target を繋ぎ直す。
    ForEachWithID([&](EntityID camID, const VirtualCamera & /*vcam*/) {
        if (auto *follow = m_world->GetComponent<CameraBodyFollow>(camID))
            BindIfInvalid(m_world, follow->target, playerId);

        if (auto *aim = m_world->GetComponent<CameraAimLookAt>(camID))
            BindIfInvalid(m_world, aim->target, playerId);

        if (auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID))
            BindIfInvalid(m_world, tps->target, playerId);
    });
}

// カメラ本体(R01_Camera=100)より必ず前に走らせる。
REGISTER_RENDER_SYSTEM(CameraTargetBindSystem, Priority::RenderStage::R00b_CameraTargetBind);
