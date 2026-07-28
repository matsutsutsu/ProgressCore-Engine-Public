// --- AudioCleanupSystem.cpp ---
#include "AudioCleanupSystem.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <memory>

using namespace CCL::ECS;

void AudioCleanupSystem::Update(float /*dt*/) {
    if (!_world) return;
    ExecuteCleanup(*_world);
}

// DestroyTag が付いた AudioEmitter を直接チャンク走査で狙い撃ち、鳴っている音を止める。
// JoltCleanupSystem::ExecuteCleanup と同じ方式（破棄確定の前に確実に後始末する）。
void AudioCleanupSystem::ExecuteCleanup(CCL::ECS::World& world)
{
    if (!world.HasResource<std::shared_ptr<IAudioAPI>>()) return;
    auto& api = world.GetResource<std::shared_ptr<IAudioAPI>>();
    if (!api) return;

    auto& chunks = world.GetChunkManager().GetChunks();
    for (auto& chunkPtr : chunks) {
        if (!chunkPtr) continue;
        auto* chunk = chunkPtr.get();

        // AudioEmitter を持ち、かつ DestroyTag がついているチャンクだけを対象にする。
        if (!chunk->HasComponent<Tag::DestroyTag>() || !chunk->HasComponent<AudioEmitterComponent>()) continue;

        size_t count = chunk->GetEntityCount();
        auto* emitters = chunk->GetComponentArray<AudioEmitterComponent>();

        for (size_t i = 0; i < count; ++i) {
            if (chunk->IsEntityDestroyed(i)) continue;

            AudioEmitterComponent& e = emitters[i];
            if (e.playingId != 0) {
                // フェードアウトは待たず即停止（実体がもう消えるため）。
                api->StopEvent(e.playingId, /*allowFadeOut*/ false);
            }
            // 二重処理防止のため実行時状態を無効化しておく。
            e.playingId  = 0;
            e.isPlaying  = false;
            e.activeHash = 0;
        }
    }
}

// 通常のゲーム内破棄（RequestDestroy → DestroyTag）を毎フレーム回収する。
REGISTER_LOGIC_SYSTEM(AudioCleanupSystem, Priority::LogicStage::L07_Cleanup);
