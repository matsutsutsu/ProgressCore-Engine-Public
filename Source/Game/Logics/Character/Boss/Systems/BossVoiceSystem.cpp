#include "BossVoiceSystem.h"

#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Audio/IAudioAPI.h"

#include "Game/Logics/Character/Player/Core/PlayerComponent.h"

#include <memory>

#include <imgui.h>

using namespace CCL::ECS;

namespace
{
    // 場面ごとのセリフの候補

    // 戦闘開始時のセリフ
    const std::vector<const char *> kBattleStartLines =
    {
        "sfx:/Muriel_Catchphrase_020", // 心拍、呼吸、体温、血圧。お前の体が、狙いを教えてくれる
        "sfx:/Muriel_Emote_Taunt_020", // 勝てはしない。だが、試すのは自由だ
    };
    // 第２形態遷移時のセリフ
    const std::vector<const char *> kTransitionLines =
    {
        "sfx:/Muriel_Kill_Revenge_010",  // 私を止めることはできない
        "sfx:/Muriel_Status_Defeat_030", // 負けても、壊れてはいない
    };
    // ボスが死んだときのセリフ
    const std::vector<const char *> kDefeatedLines = {
        "sfx:/Muriel_Death_020", // まだだ！
        "sfx:/Muriel_Death_050", // そんな…！
    };
    // プレイヤーが死んだときのセリフ
    const std::vector<const char *> kPlayerDownLines = {
        "sfx:/Muriel_Kill_Revenge_020", // 脅威を排除。任務に戻る
        "sfx:/Muriel_Kill_Enemy_010",   // 休め
    };
    // 怒りに入ったときの、いら立ちの声
    const std::vector<const char *> kRageLines =
    {
        "sfx:/Muriel_Effort_Frustration_05",
        "sfx:/Muriel_Effort_Frustration_10",
        "sfx:/Muriel_Effort_Frustration_16",
    };
    // 疲れたときのセリフ
    const std::vector<const char *> kExhaustedLines =
    {
        "sfx:/Muriel_Ability_OnCooldown_040",    // まだ充電中
        "sfx:/Muriel_Ability_OnCooldown_010",    // 時間が要る
        "sfx:/Muriel_Ability_UsedLastCharge_010", // エネルギー切れ
    };

    constexpr float kVoiceVolume      = 1.0f; // セリフの音量の倍率
    constexpr float kBattleStartDelay = 1.0f; // 第1形態に入ってから、戦闘開始のセリフまで [秒]
}



std::vector<TypeID> BossVoiceSystem::GetReadTypes() const
{
    auto types = Base::GetReadTypes();
    types.push_back(TypeInfo<PlayerComponent>::ID()); // プレイヤーを探す（HP は HealthComponent で読む）
    return types;
}



void BossVoiceSystem::Update(float dt)
{
    // 処理フロー:
    // 1. ボスの形態の変わり目で、移行と撃破のセリフ
    // 2. 第1形態が少し続いたら、戦闘開始のセリフ
    // 3. プレイヤーが倒れたら、倒したセリフ
    // 4. 怒りに入ったら、いら立ちの声。疲れたら、疲れのセリフ

    const bool cinematic = m_world->HasResource<GameFlowState>()
          && m_world->GetResource<GameFlowState>().cinematic;

    ForEach([&](const BossPhaseComponent &phase, const HealthComponent & /*health*/,
                const BossCommandComponent &cmd, const BossActionComponent &action) {
        // 最初のフレームは、今の状態を覚えるだけ
        if (!m_hasPrev)
        {
            m_hasPrev    = true;
            m_prevPhase  = phase.phase;
            m_prevAction = action.currentState;
            m_prevRaging = cmd.raging;
            return;
        }

        // ---------------------------------------------------------
        // 1. 移行と撃破
        // ---------------------------------------------------------
        if (phase.phase != m_prevPhase)
        {
            if (phase.phase == BossPhase::Transition) PlayRandom(kTransitionLines);
            if (phase.phase == BossPhase::Dead) PlayRandom(kDefeatedLines);
        }
        m_prevPhase = phase.phase;

        // ---------------------------------------------------------
        // 2. 戦闘開始
        // ---------------------------------------------------------

        // チュートリアルの後や登場のムービーの後に言わせるため、第1形態がムービーなしで続いた時間で決める
        if (phase.phase == BossPhase::Combat && !cinematic)
        {
            m_combatTime += dt;
        }
        else
        {
            m_combatTime = 0.0f;
        }

        // 戦闘開始時に行うセリフ
        if (!m_saidBattle && m_combatTime >= kBattleStartDelay)
        {
            m_saidBattle = true;
            PlayRandom(kBattleStartLines);
        }

        // ---------------------------------------------------------
        // 3. プレイヤーを倒した
        // ---------------------------------------------------------

        // ボスが先に倒れていたら言わない
        if (!m_saidPlayerDown && phase.phase != BossPhase::Dead && IsPlayerDown())
        {
            m_saidPlayerDown = true;
            PlayRandom(kPlayerDownLines);
        }

        // ---------------------------------------------------------
        // 4. 怒りと疲れ
        // ---------------------------------------------------------

        // どちらも入った瞬間だけ鳴らす。怒りの連発では、怒りに入るたびに鳴る
        if (cmd.raging && !m_prevRaging) PlayRandom(kRageLines);

        // 疲れに入った瞬間
        if (action.currentState == BossActionState::Exhausted && m_prevAction != BossActionState::Exhausted)
        {
            PlayRandom(kExhaustedLines);
        }

        m_prevRaging = cmd.raging;
        m_prevAction = action.currentState;
    });
}



void BossVoiceSystem::PlayRandom(const std::vector<const char *> &lines)
{
    if (lines.empty()) return;
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;

    auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>();
    if (!audio) return;

    // 指定のvectorのサイズまでの範囲をランダムに選ぶ
    std::uniform_int_distribution<int> pick(0, static_cast<int>(lines.size()) - 1);
    const int                          index = pick(m_random);
    audio->PlayOneShot(CCL::Utils::HashString(lines[index]), kVoiceVolume, 0.0f);
}



bool BossVoiceSystem::IsPlayerDown() const
{
    bool down = false;
    m_world->ViewEach<PlayerComponent, HealthComponent>([&](EntityID id) {
        const auto *hp = m_world->GetComponent<HealthComponent>(id);
        if (hp && hp->currentHealth <= 0.0f) down = true;
    });
    return down;
}



void BossVoiceSystem::OnGui()
{
    ImGui::Text("戦闘開始を言った : %s", m_saidBattle ? "yes" : "no");
    ImGui::Text("倒したを言った : %s", m_saidPlayerDown ? "yes" : "no");

    ImGui::SeparatorText("試しにしゃべらせる");
    if (ImGui::Button("戦闘開始")) PlayRandom(kBattleStartLines);
    ImGui::SameLine();
    if (ImGui::Button("移行")) PlayRandom(kTransitionLines);
    ImGui::SameLine();
    if (ImGui::Button("撃破")) PlayRandom(kDefeatedLines);
    ImGui::SameLine();
    if (ImGui::Button("プレイヤーを倒した")) PlayRandom(kPlayerDownLines);
    if (ImGui::Button("怒り")) PlayRandom(kRageLines);
    ImGui::SameLine();
    if (ImGui::Button("疲れ")) PlayRandom(kExhaustedLines);
}



// 形態が決まる L02 と、ダメージで HP が減る L06 の後
REGISTER_LOGIC_SYSTEM(BossVoiceSystem, Priority::LogicStage::L07_Cleanup);
