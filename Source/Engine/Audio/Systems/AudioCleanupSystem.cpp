// --- AudioCleanupSystem.cpp ---
#include "AudioCleanupSystem.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Transform/Systems/HierarchyCleanupSystem.h" // 「誰が消えるか」の唯一の定義元

#include <memory>
#include <vector>

using namespace CCL::ECS;

void AudioCleanupSystem::Update(float /*dt*/)
{
    if (!m_world) return;
    ExecuteCleanup(*m_world);
}

// 消えるEntityの AudioEmitter を狙い撃ち、鳴っている音を止める。
// JoltCleanupSystem::ExecuteCleanup と同じ方式（破棄確定の前に確実に後始末する）。
void AudioCleanupSystem::ExecuteCleanup(CCL::ECS::World &world)
{
    if (!world.HasResource<std::shared_ptr<IAudioAPI>>()) return;
    auto &api = world.GetResource<std::shared_ptr<IAudioAPI>>();
    if (!api) return;

    // DestroyTag で絞り込んではいけない。DestroyTag は「削除の起点」にしか付かず、
    //   子孫は HierarchyCleanupSystem が直接破棄するため、チャンクを DestroyTag で
    //   絞ると子Entityの鳴っている音を取りこぼす（実体が消えた後も鳴り続ける）。
    //   「誰が消えるか」の判定は HierarchyCleanupSystem に一本化する。
    std::vector<EntityID> doomed;
    doomed.reserve(64);
    HierarchyCleanupSystem::CollectDoomedEntities(world, doomed);

    for (EntityID id : doomed)
    {
        // 破棄済みの席なら GetComponent が nullptr を返すので、それが有効判定を兼ねる
        auto *e = world.GetComponent<AudioEmitterComponent>(id);
        if (!e) continue;

        if (e->playingId != 0)
        {
            // フェードアウトは待たず即停止（実体がもう消えるため）。
            api->StopEvent(e->playingId, /*allowFadeOut*/ false);
        }
        // 二重処理防止のため実行時状態を無効化しておく。
        e->playingId  = 0;
        e->isPlaying  = false;
        e->activeHash = 0;
    }
}

// 通常のゲーム内破棄（RequestDestroy → DestroyTag）を毎フレーム回収する。
REGISTER_LOGIC_SYSTEM(AudioCleanupSystem, Priority::LogicStage::L07_Cleanup);
