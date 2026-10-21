#include "CombatSoundSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"

#include "Game/Core/GameEvents.h"

#include <memory>

using namespace CCL::ECS;

void CombatSoundSystem::Initialize()
{
    // 当たったときだけ出るイベント（無敵ですり抜けたときは出ない）なので、空振りでは鳴らない
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &e) {
        Play(e.isPlayer ? "sfx:/player_hurt" : "sfx:/hit_slash");
    });

    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        Play("sfx:/just_evade");
    });

    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &) {
        Play("sfx:/dash");
    });
}

void CombatSoundSystem::Update(float /*dt*/)
{
    // 毎フレームの処理は無い。イベントを購読するためだけに System にしている。
}

void CombatSoundSystem::Play(const char *eventName)
{
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>())
    {
        return;
    }
    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
    {
        audio->PlayOneShot(CCL::Utils::HashString(eventName));
    }
}

// 処理はイベントで行う。PlayerActionVfxSystem と同じ L07_Cleanup に置く。
REGISTER_LOGIC_SYSTEM(CombatSoundSystem, Priority::LogicStage::L07_Cleanup);
