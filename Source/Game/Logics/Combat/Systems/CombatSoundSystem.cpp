#include "CombatSoundSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"

#include "Game/Core/GameEvents.h"

#include <memory>
#include <string>

using namespace CCL::ECS;

namespace
{
    // 音ごとの音量の倍率
    constexpr float kPlayerHurtVolume      = 1.3f;
    constexpr float kBossHurtVolume        = 1.6f;
    constexpr float kJustEvadeVolume       = 2.0f;
    constexpr float kJustEvadeSlowVolume   = 0.9f;
    constexpr float kPlayerDashVolume      = 1.0f;
    constexpr float kPlayerJumpVolume      = 1.5f;

    // ジャンプの段ごとのピッチ（オクターブ）。1段目は地面を蹴るので低く重く、2段目は空中なので高く軽く
    constexpr float kJumpPitchGround = -0.15f;
    constexpr float kJumpPitchAir    = 0.1f;

    // 回数が多い音の揺らぎ。毎回同じに聞こえないようにする
    constexpr float kPitchJitter  = 0.06f; // ピッチの揺れ幅（±オクターブ。0.06 で約 ±4%）
    constexpr float kVolumeJitter = 0.1f;  // 音量の揺れ幅（±倍率）
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

    // ジャスト回避は金属の「キィン」と、続くラッシュのスローに合わせた低い音を重ねる
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        Play("sfx:/SE_PlayerJustEvade", kJustEvadeVolume);
        Play("sfx:/SE_PlayerJustEvadeSlow", kJustEvadeSlowVolume);
    });

    // ダッシュは回数が多いので、一番短い風切り音
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &) {
        PlayVaried(m_dashSound, kPlayerDashVolume, 0.0f);
    });

    ListenEvent<PlayerJumpedEvent>([this](const PlayerJumpedEvent &e) {
        const float pitch = (e.jumpCount >= 2) ? kJumpPitchAir : kJumpPitchGround;
        PlayVaried(m_jumpSound, kPlayerJumpVolume, pitch);
    });
}



void CombatSoundSystem::Update(float /*dt*/)
{
    // 毎フレームの処理はない。イベントを購読するためだけに System にしている
}



// イベント名の効果音を、音量の倍率とピッチを指定して1回鳴らす
void CombatSoundSystem::Play(const char *eventName, float volume, float pitch)
{
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>())
    {
        return;
    }
    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
    {
        audio->PlayOneShot(CCL::Utils::HashString(eventName), volume, pitch);
    }
}



void CombatSoundSystem::PlayVaried(VariedSound &sound, float volume, float pitch)
{
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>())
    {
        return;
    }
    auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
    if (!audio)
    {
        return;
    }

    // 音は起動時に読み込まれるので、最初に鳴らすときに1度だけ集めればよい
    if (!sound.collected)
    {
        CollectVariants(sound, *audio);
    }

    // 1. 候補を選ぶ（2つ以上あれば、直前と同じものは避ける）
    const int count = static_cast<int>(sound.hashes.size());
    int       index = 0;
    if (count > 1 && sound.last >= 0)
    {
        // 直前の1つを除いた中から選び、直前以降の番号は1つずらす
        index = std::uniform_int_distribution<int>(0, count - 2)(m_random);
        if (index >= sound.last) index++;
    }
    else if (count > 1)
    {
        index = std::uniform_int_distribution<int>(0, count - 1)(m_random);
    }
    sound.last = index;

    // 2. ピッチと音量を少し揺らす
    const float pitchJitter  = std::uniform_real_distribution<float>(-kPitchJitter, kPitchJitter)(m_random);
    const float volumeJitter = std::uniform_real_distribution<float>(-kVolumeJitter, kVolumeJitter)(m_random);

    audio->PlayOneShot(sound.hashes[index], volume * (1.0f + volumeJitter), pitch + pitchJitter);
}



void CombatSoundSystem::CollectVariants(VariedSound &sound, const IAudioAPI &audio)
{
    sound.collected = true;

    const std::string base   = sound.baseEvent;
    const std::string prefix = base + "_";
    for (const std::string &name : audio.GetEventNames())
    {
        if (name == base || name.rfind(prefix, 0) == 0)
        {
            sound.hashes.push_back(CCL::Utils::HashString(name.c_str()));
        }
    }

    // 一覧を返さない音声でも、元の1つは鳴らせるようにする
    if (sound.hashes.empty())
    {
        sound.hashes.push_back(CCL::Utils::HashString(sound.baseEvent));
    }
}



// 処理はイベントで行う。PlayerActionVfxSystem と同じ L07_Cleanup に置く
REGISTER_LOGIC_SYSTEM(CombatSoundSystem, Priority::LogicStage::L07_Cleanup);
