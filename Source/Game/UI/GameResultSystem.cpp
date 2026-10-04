#include "GameResultSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/ScreenHeadingUi.h" // 左上の見出し・線・記録の行（ポーズと共通の組み）
#include "Engine/Core/App/GameFlowState.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Game/Logics/Combat/EnemyTag.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/AI/Battle/BattleContext.h"           // プレイヤーとボスの EntityID
#include "Game/Logics/AI/Drone/Components/DroneComponent.h" // 群れの機数を数える
#include "Game/Logics/GameEvent/GameEvents.h"              // 命中・被弾・ジャスト回避のイベント

#include <algorithm> // std::clamp / std::max
#include <cmath>     // std::lround
#include <cstdint>
#include <cstdio>    // std::snprintf

#include <imgui.h>

using namespace CCL::ECS;

namespace
{
    // --- 演出の時間（リザルトを出してからの秒） ---
    constexpr float kSubStart  = 0.35f; // 線の下の1行を打ち始める
    constexpr float kRowStart  = 0.6f;  // 記録の1行目を打ち始める
    constexpr float kRowStep   = 0.16f; // 記録の行ごとの時間差
    constexpr float kMenuDelay = 0.6f;  // 記録の最後の行を打ち始めてから、ボタンが出るまで
    constexpr float kMenuGap   = 60.0f; // 記録の最後の行からボタンの入れ物の上端まで（px）
} // namespace

void GameResultSystem::Initialize()
{
    // ポーズなどと共有するゲーム進行の状態。無ければ作る
    if (!m_world->HasResource<GameFlowState>())
        m_world->AddResource<GameFlowState>();

    // ボタンのクリックを購読する。ポーズのボタンも同じイベントを出すので、actionHash で見分ける
    const uint32_t kRetry = CCL::Utils::HashString("retry");
    const uint32_t kTitle = CCL::Utils::HashString("result_title");

    ListenEvent<UIButtonClickedEvent>(
        [this, kRetry, kTitle](const UIButtonClickedEvent &e) {
            if (m_transitioning) return;

            if (e.actionHash == kRetry)
            {
                // ゲームを読み直す。フェードと裏での読み込みは SceneManager が行う
                m_transitioning = SceneManager::Instance().RequestTransition("Game", true);
            }
            else if (e.actionHash == kTitle)
            {
                // タイトルへ戻る。アプリの終了はタイトル画面の QUIT だけにしてある。
                // この購読はワーカースレッドで走ることがあるので、World を片付けるのは
                // ここではなく SceneManager に任せる
                m_transitioning = SceneManager::Instance().RequestTransition("Title", true);
            }
        });

    // --- 戦いの記録。結果が出た後の分は数えない ---
    ListenEvent<AttackHitConfirmedEvent>([this](const AttackHitConfirmedEvent &e) {
        if (!m_resultShown && e.attackerID == m_playerID.load()) ++m_hits;
    });
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &e) {
        if (!m_resultShown && e.isPlayer) m_damageX10 += static_cast<int>(std::lround(e.damageAmount * 10.0f));
    });
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        if (!m_resultShown) ++m_justEvades;
    });
}

void GameResultSystem::Update(float dt)
{
    if (!m_debugEnabled) return; // ステージを確かめている間などに判定を止める（デバッグ用）

    if (m_resultShown) return; // 出てくる演出は UIRevealSystem が進める

    // 勝った後の待ち。dt はスローの影響を受けない実時間なので、数え終わればスローも終わっている
    if (m_pendingWin)
    {
        m_winDelay -= dt;
        if (m_winDelay <= 0.0f)
        {
            m_resultShown = true;
            ShowResultUI(true); // MISSION COMPLETE
            CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear. Result UI shown (after defeat slow-mo).");
        }
        return; // 待っている間は判定し直さない
    }

    bool playerPresent = false;
    bool playerDead    = false;
    bool anyEnemy      = false;
    int  aliveEnemies  = 0;

    // HealthComponent を持つエンティティを、プレイヤーと敵に分けて数える。
    // 敵かどうかは EnemyTag だけで決める（理由は EnemyTag.h を参照）
    ForEachWithID([&](EntityID id, const HealthComponent &hp) {
        const bool dead = (hp.currentHealth <= 0.0f) || m_world->HasComponent<Tag::DeadTag>(id);

        if (m_world->HasComponent<PlayerComponent>(id))
        {
            playerPresent = true;
            if (dead) playerDead = true;
        }
        else if (m_world->HasComponent<Tag::EnemyTag>(id))
        {
            anyEnemy = true;
            if (!dead) ++aliveEnemies;
        }
    });

    if (anyEnemy) m_sawEnemy = true;

    // OnGui で表示するために控えておく
    m_dbgPlayerPresent = playerPresent;
    m_dbgAliveEnemies  = aliveEnemies;

    // プレイヤーが居ないシーンでは判定しない。ステージを確かめるためのシーン（プレイヤーを置かない）で、
    // 敵として数えた HealthComponent が 0 になった瞬間に GAME CLEAR が出てしまうため
    if (!playerPresent) return;

    CountBattleStats(dt);

    // 負けを先に判定する
    if (playerDead)
    {
        m_resultShown = true;
        ShowResultUI(false); // MISSION FAILED
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Over. Result UI shown.");
    }
    else if (m_sawEnemy && aliveEnemies == 0)
    {
        // すぐには出さず、スローが終わってから出す（出すのは上の待ちの処理）
        m_pendingWin = true;
        m_winDelay   = kResultDelay;
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear detected. Waiting for defeat slow-mo before showing result.");
    }
}

void GameResultSystem::CountBattleStats(float dt)
{
    // Update はロジックの帯で走るので、ポーズ中は呼ばれない（時間も数えない）
    m_playTime += dt;

    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    m_playerID.store(battle.playerEntity);

    // 切り払い: 突進に入った瞬間を1回と数え、チェインの最大段数を控える
    if (const auto *cleave = battle.HasPlayer() ? m_world->GetComponent<CleaveComponent>(battle.playerEntity) : nullptr)
    {
        if (cleave->phase == CleavePhase::Lunging && m_prevCleave != CleavePhase::Lunging) ++m_cleaves;
        m_maxChain   = std::max(m_maxChain, cleave->chainCount);
        m_prevCleave = cleave->phase;
    }
}

void GameResultSystem::ShowResultUI(bool win)
{
    using namespace CCL::GameUI;
    World &w = *m_world;

    // --- 結果が出た時点の値 ---
    int swarm = 0;
    w.ViewEach<DroneComponent>([&swarm](EntityID) { ++swarm; });
    m_swarmMax = std::max(m_swarmMax, swarm);

    if (w.HasResource<BattleContext>())
    {
        const auto &battle = w.GetResource<BattleContext>();
        if (const auto *hp = battle.HasBoss() ? w.GetComponent<HealthComponent>(battle.bossEntity) : nullptr)
            m_bossHpRatio = std::clamp(hp->currentHealth / std::max(hp->maxHealth, 1.0f), 0.0f, 1.0f);
    }

    // 以降の UI はすべてこの Canvas の子にする。リザルトはシーンを切り替えるまで消さないので、ID は持たない
    const EntityID        canvas = SpawnCanvas(w, "ResultCanvas");
    std::vector<EntityID> spawned;

    // 暗転。下ほど濃くして、左上の文字を読みやすくする（フェードだけ・スライドしない）
    const DirectX::XMFLOAT4 dimBottom{0.0f, 0.0f, 0.0f, 0.85f};
    SpawnPanel(w, "ResultDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.55f}, 200,
               0.00f, 0.0f, canvas, &dimBottom);

    // 見出しと線。勝ちは白金、負けは深紅
    SpawnScreenHeading(w, canvas, win ? "MISSION COMPLETE" : "MISSION FAILED",
                       win ? ScreenColor::kGold : ScreenColor::kRed, spawned);

    // 線の下の1行: 勝ちなら撃破、負けならボスの残り HP
    char sub[96];
    if (win)
        std::snprintf(sub, sizeof(sub), "TARGET : MURIEL  //  NEUTRALIZED");
    else
        std::snprintf(sub, sizeof(sub), "TARGET : MURIEL  //  HP %d%% REMAINING",
                      static_cast<int>(m_bossHpRatio * 100.0f + 0.5f));
    SpawnTypedText(w, "ResultSub", sub, ScreenLayout::kLeft, ScreenLayout::kSubY, 20.0f, ScreenColor::kSub, kSubStart,
                   canvas);

    // 記録の行
    const int   minutes = static_cast<int>(m_playTime) / 60;
    const float seconds = m_playTime - static_cast<float>(minutes * 60);
    char        buf[6][48];
    std::snprintf(buf[0], sizeof(buf[0]), "%02d:%05.2f", minutes, seconds);
    std::snprintf(buf[1], sizeof(buf[1]), "%d", m_hits.load());
    std::snprintf(buf[2], sizeof(buf[2]), "%d", (m_damageX10.load() + 5) / 10);
    std::snprintf(buf[3], sizeof(buf[3]), "%d", m_justEvades.load());
    std::snprintf(buf[4], sizeof(buf[4]), "%d   MAX CHAIN %d", m_cleaves, m_maxChain);
    std::snprintf(buf[5], sizeof(buf[5]), "%d UNITS", m_swarmMax);

    const char *labels[6] = {"TIME", "HITS", "DAMAGE TAKEN", "JUST EVADE", "CLEAVE", "SWARM ENGAGED"};
    const char *names[6]  = {"ResultTime", "ResultHits", "ResultDamage", "ResultEvade", "ResultCleave", "ResultSwarm"};
    for (int i = 0; i < 6; ++i)
        SpawnScreenRow(w, canvas, names[i], i, labels[i], buf[i], kRowStart + kRowStep * static_cast<float>(i), spawned);

    // --- ボタンと操作のヒント（記録が出そろってから出す） ---
    const float    menuTop   = ScreenLayout::kRowY + ScreenLayout::kRowH * 5.0f + ScreenLayout::kRowPx + kMenuGap;
    const float    menuDelay = kRowStart + kRowStep * 5.0f + kMenuDelay;
    const EntityID menu      = SpawnBox(w, "ResultMenu", {0.0f, 0.0f}, {ScreenLayout::kLeft, menuTop}, {320.0f, 140.0f}, canvas);

    // 最初は RETRY を選んでおく
    SpawnButton(w, "BtnRetry", "LblRetry", "RETRY", {0.0f, -42.0f}, "retry", true, 230, menuDelay, menu);
    SpawnButton(w, "BtnResultTitle", "LblResultTitle", "TITLE", {0.0f, 43.0f}, "result_title", false, 230,
                menuDelay + 0.08f, menu);
    SpawnMenuHints(w, menu, 125.0f, spawned, 240, menuDelay + 0.16f);
}

void GameResultSystem::OnGui()
{
    ImGui::SeparatorText("リザルト検知 (デバッグ)");

    ImGui::Checkbox("システム有効", &m_debugEnabled);
    if (!m_debugEnabled)
        ImGui::TextDisabled("→ 勝敗判定・リザルト表示を停止中");

    ImGui::Separator();
    ImGui::Text("プレイヤー存在 : %s", m_dbgPlayerPresent ? "yes" : "NO (テストScene扱い→抑止)");
    ImGui::Text("敵を観測(sawEnemy) : %s", m_sawEnemy ? "yes" : "no");
    ImGui::Text("生存敵数 : %d", m_dbgAliveEnemies);
    ImGui::Text("リザルト表示済み : %s", m_resultShown ? "yes" : "no");
    ImGui::Text("勝利待機中 : %s (残り %.2fs)", m_pendingWin ? "yes" : "no", m_winDelay);

    ImGui::SeparatorText("戦いの記録");
    ImGui::Text("時間 %.2fs / 命中 %d / 被ダメージ %.1f / ジャスト回避 %d / 切り払い %d (最大チェイン %d)",
                m_playTime, m_hits.load(), m_damageX10.load() / 10.0f, m_justEvades.load(), m_cleaves, m_maxChain);

    ImGui::Separator();
    if (ImGui::Button("検知状態をリセット"))
    {
        m_resultShown   = false;
        m_sawEnemy      = false;
        m_pendingWin    = false;
        m_winDelay      = 0.0f;
        m_transitioning = false;
    }
    ImGui::SameLine();
    // 戦わずにリザルトの見た目を確かめる（今の記録のまま出す）
    if (!m_resultShown && ImGui::Button("勝ちのリザルトを出す"))
    {
        m_resultShown = true;
        ShowResultUI(true);
    }
    ImGui::SameLine();
    if (!m_resultShown && ImGui::Button("負けのリザルトを出す"))
    {
        m_resultShown = true;
        ShowResultUI(false);
    }
    ImGui::TextDisabled("※プレイヤー不在のSceneでは自動でリザルトを抑止します");
}
