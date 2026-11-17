#include "CombatSoundSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"

#include "Game/Core/GameEvents.h"

#include <memory>
#include <string>

#include <imgui.h>

using namespace CCL::ECS;



void CombatSoundSystem::Initialize()
{
    // 当たったときだけ出るイベント（無敵ですり抜けたときは出ない）なので、空振りでは鳴らない
    // プレイヤーの被弾は広がりのある重い衝撃、ボスへの命中は短く鋭い衝撃（連続で当たっても濁らない）
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &e) {
        if (e.isPlayer)
            Play("sfx:/SE_PlayerHurt", m_playerHurt);
        else
            Play("sfx:/SE_BossHurt", m_bossHurt);
    });

    // ジャスト回避は金属の「キィン」と、続くラッシュのスローに合わせた低い音を重ねる
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        Play("sfx:/SE_PlayerJustEvade", m_justEvade);
        Play("sfx:/SE_PlayerJustEvadeSlow", m_justEvadeSlow);
    });

    // ダッシュはジャンプと同じ風切り音（SE_PlayerJump）
    ListenEvent<PlayerDashedEvent>([this](const PlayerDashedEvent &) {
        PlayVaried(m_dashSound, m_dash);
    });

    ListenEvent<PlayerJumpedEvent>([this](const PlayerJumpedEvent &e) {
        PlayVaried(m_jumpSound, (e.jumpCount >= 2) ? m_jumpAir : m_jumpGround);
    });
}



void CombatSoundSystem::Update(float dt)
{
    // 毎フレームの処理はない。イベントを購読するためだけに System にしている
}



void CombatSoundSystem::OnGui()
{
    if (DrawSetting("被弾（プレイヤー）", m_playerHurt)) Play("sfx:/SE_PlayerHurt", m_playerHurt);
    if (DrawSetting("命中（ボス）", m_bossHurt)) Play("sfx:/SE_BossHurt", m_bossHurt);
    if (DrawSetting("ジャスト回避", m_justEvade)) Play("sfx:/SE_PlayerJustEvade", m_justEvade);
    if (DrawSetting("ジャスト回避のスロー", m_justEvadeSlow)) Play("sfx:/SE_PlayerJustEvadeSlow", m_justEvadeSlow);
    if (DrawSetting("ダッシュ", m_dash)) PlayVaried(m_dashSound, m_dash);
    if (DrawSetting("ジャンプ（1段目）", m_jumpGround)) PlayVaried(m_jumpSound, m_jumpGround);
    if (DrawSetting("ジャンプ（2段目）", m_jumpAir)) PlayVaried(m_jumpSound, m_jumpAir);

    ImGui::SeparatorText("揺らぎ（ダッシュとジャンプ）");
    ImGui::SliderFloat("ピッチの揺れ幅", &m_pitchJitter, 0.0f, 0.3f, "±%.2f オクターブ");
    ImGui::SliderFloat("音量の揺れ幅", &m_volumeJitter, 0.0f, 0.5f, "±%.2f");

    ImGui::TextDisabled("再起動で戻る。決まった値は CombatSoundSystem.h の初期値に書く");
}



bool CombatSoundSystem::DrawSetting(const char *label, SoundSetting &setting)
{
    // 同じ名前のスライダーが並ぶので、音ごとに ID を分ける
    ImGui::PushID(label);
    ImGui::SeparatorText(label);
    ImGui::SliderFloat("音量", &setting.volume, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("ピッチ", &setting.pitch, -1.0f, 1.0f, "%.2f オクターブ");
    const bool play = ImGui::Button("鳴らす");
    ImGui::PopID();
    return play;
}



// イベント名の効果音を、音量の倍率とピッチを指定して1回鳴らす
void CombatSoundSystem::Play(const char *eventName, const SoundSetting &setting)
{
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>())
    {
        return;
    }
    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
    {
        audio->PlayOneShot(CCL::Utils::HashString(eventName), setting.volume, setting.pitch);
    }
}



void CombatSoundSystem::PlayVaried(VariedSound &sound, const SoundSetting &setting)
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
        std::uniform_int_distribution<int> pick(0, count - 2);
        index = pick(m_random);
        if (index >= sound.last) index++;
    }
    else if (count > 1)
    {
        std::uniform_int_distribution<int> pick(0, count - 1);
        index = pick(m_random);
    }
    sound.last = index;

    // 2. ピッチと音量を少し揺らす
    std::uniform_real_distribution<float> pitchDist(-m_pitchJitter, m_pitchJitter);
    std::uniform_real_distribution<float> volumeDist(-m_volumeJitter, m_volumeJitter);
    const float pitchJitter  = pitchDist(m_random);
    const float volumeJitter = volumeDist(m_random);

    audio->PlayOneShot(sound.hashes[index], setting.volume * (1.0f + volumeJitter), setting.pitch + pitchJitter);
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
