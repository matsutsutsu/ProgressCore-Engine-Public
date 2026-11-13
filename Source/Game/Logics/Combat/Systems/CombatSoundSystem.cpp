#include "CombatSoundSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"

#include "Game/Core/GameEvents.h"

#include <memory>

using namespace CCL::ECS;

namespace
{
    // 音ごとの音量の倍率
    constexpr float kPlayerHurtVolume      = 1.3f;
    constexpr float kBossHurtVolume        = 1.6f;
    constexpr float kJustEvadeVolume       = 2.0f;
    constexpr float kJustEvadeSlowVolume   = 0.6f; 
    constexpr float kPlayerDashVolume      = 1.0f;
}



void CombatSoundSystem::Initialize()
{
    // 当たったときだけ出るイベント（無敵ですり抜けたときは出ない）なので、空振りでは鳴らない
    // プレイヤーの被弾は広がりのある重い衝撃、ボスへの命中は短く鋭い衝撃（連続で当たっても濁らない）
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &e) {
        if (e.isPlayer)
            Play("sfx:/SE_PlayerHurt", kPlayerHurtVolume);
        else
            Play("sfx:/SE_BossHurt", kBossHurtVolume);
    });

    // ジャスト回避は金属の「キィン」と、続くラッシュのスローに合わせた低い「ヴゥン」を重ねる
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        Play("sfx:/SE_PlayerJustEvade", kJustEvadeVolume);
        Play("sfx:/SE_PlayerJustEvadeSlow", kJustEvadeSlowVolume);
    });

    // ダッシュは回数が多いので、一番短い風切り音
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &) {
        Play("sfx:/SE_PlayerDash", kPlayerDashVolume);
    });
}



void CombatSoundSystem::Update(float /*dt*/)
{
    // 毎フレームの処理はない。イベントを購読するためだけに System にしている
}



// イベント名の効果音を、音量の倍率を掛けて1回鳴らす
void CombatSoundSystem::Play(const char *eventName, float volume)
{
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>())
    {
        return;
    }
    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
    {
        // ピッチは変えない（0 で等倍）
        audio->PlayOneShot(CCL::Utils::HashString(eventName), volume, 0.0f);
    }
}



// 処理はイベントで行う。PlayerActionVfxSystem と同じ L07_Cleanup に置く
REGISTER_LOGIC_SYSTEM(CombatSoundSystem, Priority::LogicStage::L07_Cleanup);
