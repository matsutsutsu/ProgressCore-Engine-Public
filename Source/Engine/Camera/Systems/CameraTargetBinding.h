#pragma once
/// @file  CameraTargetBinding.h
/// @brief カメラの追従/注視ターゲットを「役割(PlayerComponent)」で解決する共通ヘルパー。
///
///   ハードコードした EntityID ではなく役割で繋ぐため、プレハブ/レベルで
///   プレイヤーとカメラを別々に配置しても、save/load やリスポーンで ID が
///   変わっても、実行時に自動でカメラ target をプレイヤーへ繋ぎ直せる。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

namespace CameraBinding {

    // PlayerComponent を持つ最初のエンティティ(=プレイヤー)を役割で探す。
    inline CCL::ECS::EntityID FindPlayer(CCL::ECS::World* world)
    {
        using namespace CCL::ECS;
        if (!world) return InvalidEntityID;
        for (auto& chunkPtr : world->GetChunkManager().GetChunks()) {
            if (!chunkPtr) continue;
            Chunk* chunk = chunkPtr.get();
            if (!chunk->HasComponent<PlayerComponent>()) continue;
            for (size_t i = 0; i < chunk->GetEntityCount(); ++i) {
                if (chunk->IsEntityDestroyed(i)) continue;
                return chunk->GetEntityByIndex(i);
            }
        }
        return InvalidEntityID;
    }

    // target が無効(未接続 / リロードでID変化 / リスポーン)なら player へ繋ぎ直す。
    //   有効な相手を指している場合(手動設定など)は尊重して上書きしない。
    inline void BindIfInvalid(CCL::ECS::World* world,
        CCL::Serialization::EntityLink& target, CCL::ECS::EntityID player)
    {
        if (player == CCL::ECS::InvalidEntityID) return;
        if (!world->IsEntityValid(target.id)) target.id = player;
    }

} // namespace CameraBinding
