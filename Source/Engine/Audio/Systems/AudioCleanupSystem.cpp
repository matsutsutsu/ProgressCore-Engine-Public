#include "AudioCleanupSystem.h"
#include "Engine/Audio/IAudioAPI.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Transform/Systems/HierarchyCleanupSystem.h"

#include <memory>
#include <vector>

using namespace CCL::ECS;

void AudioCleanupSystem::Update(float /*dt*/)
{
    if (!m_world) return;
    ExecuteCleanup(*m_world);
}



void AudioCleanupSystem::ExecuteCleanup(CCL::ECS::World &world)
{
    if (!world.HasResource<std::shared_ptr<IAudioAPI>>()) return;
    auto &api = world.GetResource<std::shared_ptr<IAudioAPI>>();
    if (!api) return;

    // DestroyTag で絞り込んではいけない。DestroyTag は削除の起点にしか付かず、
    //   子孫は HierarchyCleanupSystem が直接破棄するため、DestroyTag で絞ると
    //   子 Entity の音を止め損ねる（Entity が消えた後も鳴り続ける）
    std::vector<EntityID> doomed;
    doomed.reserve(64);
    HierarchyCleanupSystem::CollectDoomedEntities(world, doomed);

    for (EntityID id : doomed)
    {
        // 破棄済みの ID なら GetComponent が nullptr を返すので、それが有効判定を兼ねる
        auto *e = world.GetComponent<AudioEmitterComponent>(id);
        if (!e) continue;

        if (e->playingId != 0)
        {
            // Entity がもう消えるので、最後まで鳴らさず即停止する
            api->StopEvent(e->playingId, /*allowFadeOut*/ false);
        }
        // 二重に止めないよう実行時の状態を無効化しておく
        e->playingId  = 0;
        e->isPlaying  = false;
        e->activeHash = 0;
    }
}



// ゲーム中の通常の破棄（RequestDestroy → DestroyTag）は毎ステップここで拾う
REGISTER_LOGIC_SYSTEM(AudioCleanupSystem, Priority::LogicStage::L07_Cleanup);
