#include "Game/UI/Screens/GameResultSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/Common/ScreenHeadingUi.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Game/Logics/Combat/Components/DeadTag.h"
#include "Game/Logics/Combat/Components/EnemyTag.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Core/GameEvents.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <imgui.h>

using namespace CCL::ECS;

namespace
{
    // --- 演出の時間（秒。リザルトを出してから） ---
    constexpr float kSubStart  = 0.35f; // 線の下の1行を打ち始めるまで
    constexpr float kRowStart  = 0.6f;  // 記録の1行目を打ち始めるまで
    constexpr float kRowStep   = 0.16f; // 記録の行ごとの時間差
    constexpr float kMenuDelay = 0.6f;  // 記録の最後の行を打ち始めてから、ボタンが出るまで

    constexpr float kMenuGap = 60.0f; // 記録の最後の行から、ボタンの入れ物の上端までの間隔（px）

    constexpr int kRowCount = 4; // 記録の行の数
} // namespace



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void GameResultSystem::Initialize()
{
    // ポーズなどと共有するゲーム進行の状態。なければ作る
    if (!m_world->HasResource<GameFlowState>()) m_world->AddResource<GameFlowState>();

    // --- ボタン ---
    // ポーズのボタンも同じイベントを出すので、actionHash で見分ける
    const uint32_t kRetry = CCL::Utils::HashString("retry");
    const uint32_t kTitle = CCL::Utils::HashString("result_title");

    ListenEvent<UIButtonClickedEvent>(
        [this, kRetry, kTitle](const UIButtonClickedEvent &e) {
            if (m_transitioning) return;

            // 切り替えを頼むだけにする。フェードと裏での読み込みは SceneManager が行う
            // この購読はワーカースレッドで走ることがあるので、World の片付けはここで行わない
            if (e.actionHash == kRetry)
            {
                m_transitioning = SceneManager::Instance().RequestTransition("Game", true);
            }
            else if (e.actionHash == kTitle)
            {
                // アプリの終了は、タイトル画面の「終了」だけにしている
                m_transitioning = SceneManager::Instance().RequestTransition("Title", true);
            }
        });

    // --- 戦いの記録 ---
    // 結果が出た後の分は数えない
    ListenEvent<AttackHitConfirmedEvent>([this](const AttackHitConfirmedEvent &e) {
        if (!m_resultShown && e.attackerID == m_playerID.load()) ++m_hits;
    });
    ListenEvent<DamageTakenEvent>([this](const DamageTakenEvent &e) {
        // atomic<float> は足し算が使えないので、10 倍して整数で足す
        if (!m_resultShown && e.isPlayer)
        {
            m_damageX10 += static_cast<int>(std::lround(e.damageAmount * 10.0f));
        }
    });
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &) {
        if (!m_resultShown) ++m_justEvades;
    });
}



void GameResultSystem::Update(float dt)
{
    // 処理フロー:
    // 1. 勝ちを判定した後なら、リザルトを出す時機を待つ
    // 2. プレイヤーと敵を数える
    // 3. 戦いの記録を数える
    // 4. 勝敗を判定する

    // ステージを確かめている間などに、判定を止められるようにする（デバッグ用）
    if (!m_debugEnabled) return;

    // リザルトを出した後の演出は、UIRevealSystem が進める
    if (m_resultShown) return;

    // ---------------------------------------------------------
    // 1. 勝った後の待ち
    // ---------------------------------------------------------

    // 勝ちはすぐには出さない。最後の敵を倒したフレームから、撃破のスローが始まるため
    // dt はスローの影響を受けない実時間なので、kResultDelay を数え終えれば、スローも終わっている
    if (m_pendingWin)
    {
        m_winDelay -= dt;

        // 撃破ムービーが流れている間は出さない。終わるかスキップされたら出す
        // ムービーの再生要求は撃破のスロー（BossPhaseComponent の defeatSlowDuration）の後に出るので、
        // kResultDelay を待ち終えた時点では cinematic がもう立っている
        // defeatSlowDuration を kResultDelay 以上に延ばすと、ムービーの前にリザルトが出る
        // ムービーがないシーンでは cinematic が立たず、kResultDelay だけ待って出る
        const bool cinematic =
            m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().cinematic;

        if (m_winDelay <= 0.0f && !cinematic)
        {
            m_resultShown = true;
            ShowResultUI(true); // MISSION COMPLETE
            CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear. Result UI shown (after defeat slow-mo).");
        }

        // 待っている間は、勝敗を判定し直さない
        return;
    }

    // ---------------------------------------------------------
    // 2. プレイヤーと敵
    // ---------------------------------------------------------
    bool playerPresent = false;
    bool playerDead    = false;
    bool anyEnemy      = false;
    int  aliveEnemies  = 0;

    // HealthComponent を持つエンティティを、プレイヤーと敵に分けて数える
    // 敵かどうかは EnemyTag だけで決める（理由は EnemyTag.h）
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

    // 敵を一度見たかを覚える。始まった直後の、まだ敵が出ていない状態（敵 0 体）を勝ちにしないため
    if (anyEnemy) m_sawEnemy = true;

    // OnGui で表示するために控えておく
    m_dbgPlayerPresent = playerPresent;
    m_dbgAliveEnemies  = aliveEnemies;

    // プレイヤーがいないシーンでは判定しない
    // プレイヤーを置かない確認用のシーンで、敵が 0 になった瞬間に勝ちが出てしまうため
    if (!playerPresent) return;

    // ---------------------------------------------------------
    // 3. 戦いの記録
    // ---------------------------------------------------------
    CountBattleStats(dt);

    // ---------------------------------------------------------
    // 4. 勝敗
    // ---------------------------------------------------------

    // 負けを先に判定する
    if (playerDead)
    {
        EndMatch();
        m_resultShown = true;
        ShowResultUI(false); // MISSION FAILED
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Over. Result UI shown.");
    }
    else if (m_sawEnemy && aliveEnemies == 0)
    {
        // リザルトはすぐには出さず、1 の「勝った後の待ち」で出す。操作はこの時点で止める
        EndMatch();
        m_pendingWin = true;
        m_winDelay   = kResultDelay;
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear detected. Waiting for defeat slow-mo before showing result.");
    }
}



void GameResultSystem::OnGui()
{
    ImGui::SeparatorText("リザルト検知 (デバッグ)");

    ImGui::Checkbox("システム有効", &m_debugEnabled);
    if (!m_debugEnabled) ImGui::TextDisabled("→ 勝敗判定・リザルト表示を停止中");

    // --- 判定の状態 ---
    ImGui::Separator();
    ImGui::Text("プレイヤー存在 : %s", m_dbgPlayerPresent ? "yes" : "NO (テストScene扱い→抑止)");
    ImGui::Text("敵を観測(sawEnemy) : %s", m_sawEnemy ? "yes" : "no");
    ImGui::Text("生存敵数 : %d", m_dbgAliveEnemies);
    ImGui::Text("リザルト表示済み : %s", m_resultShown ? "yes" : "no");
    ImGui::Text("勝利待機中 : %s (残り %.2fs)", m_pendingWin ? "yes" : "no", m_winDelay);

    // --- 戦いの記録 ---
    ImGui::SeparatorText("戦いの記録");
    ImGui::Text("時間 %.2fs / 命中 %d / 被ダメージ %.1f / ジャスト回避 %d",
                m_playTime, m_hits.load(), m_damageX10.load() / 10.0f, m_justEvades.load());

    // --- 操作 ---
    ImGui::Separator();
    if (ImGui::Button("検知状態をリセット"))
    {
        m_resultShown   = false;
        m_sawEnemy      = false;
        m_pendingWin    = false;
        m_winDelay      = 0.0f;
        m_transitioning = false;
    }

    // 戦わずにリザルトの見た目を確かめる（今の記録のまま出す）
    ImGui::SameLine();
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



// ---------------------------------------------------------------------------
// 勝敗と記録
// ---------------------------------------------------------------------------

void GameResultSystem::CountBattleStats(float dt)
{
    // Update はロジックの帯で走るので、ポーズ中は呼ばれない（時間も数えない）
    m_playTime += dt;

    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();
    m_playerID.store(battle.playerEntity);
}



void GameResultSystem::EndMatch()
{
    if (m_world->HasResource<GameFlowState>()) m_world->GetResource<GameFlowState>().matchOver = true;
}



// ---------------------------------------------------------------------------
// リザルトの画面
// ---------------------------------------------------------------------------

void GameResultSystem::ShowResultUI(bool win)
{
    // 処理フロー:
    // 1. 操作とボスを止め、結果が出た時点の値を取る
    // 2. Canvas と暗転を作る
    // 3. 見出しと、線の下の1行を作る
    // 4. 記録の行を作る
    // 5. ボタンと操作のヒントを作る

    using namespace CCL::GameUI;
    World &w = *m_world;

    // ---------------------------------------------------------
    // 1. 結果が出た時点の値
    // ---------------------------------------------------------

    // デバッグの「リザルトを出す」から呼ばれたときも、操作とボスを止める
    EndMatch();

    if (w.HasResource<BattleContext>())
    {
        const auto &battle = w.GetResource<BattleContext>();
        if (const auto *hp = battle.HasBoss() ? w.GetComponent<HealthComponent>(battle.bossEntity) : nullptr)
        {
            m_bossHpRatio = std::clamp(hp->currentHealth / std::max(hp->maxHealth, 1.0f), 0.0f, 1.0f);
        }
    }

    // ---------------------------------------------------------
    // 2. Canvas と暗転
    // ---------------------------------------------------------

    // 以降の UI はすべてこの Canvas の子にする
    // リザルトはシーンを切り替えるまで消さないので、作った ID は持っておかない
    const EntityID        canvas = SpawnCanvas(w, "ResultCanvas");
    std::vector<EntityID> spawned;

    // 暗転は下ほど濃くして、左上の文字を読みやすくする。フェードだけで、スライドはしない
    const DirectX::XMFLOAT4 dimBottom{0.0f, 0.0f, 0.0f, 0.85f};
    SpawnPanel(w, "ResultDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.55f}, 200,
               0.00f, 0.0f, canvas, &dimBottom);

    // ---------------------------------------------------------
    // 3. 見出しと、線の下の1行
    // ---------------------------------------------------------

    // 見出しの色は、勝ちは白金、負けは深紅
    const MenuStyleComponent style = GetMenuStyle(w);
    SpawnScreenHeading(w, canvas, win ? "MISSION COMPLETE" : "MISSION FAILED", win ? style.gold : style.red, spawned);

    // 線の下の1行は、勝ちなら撃破、負けならボスの残り HP
    char sub[96];
    if (win)
    {
        std::snprintf(sub, sizeof(sub), "ボス VALKYRA を撃破した");
    }
    else
    {
        std::snprintf(sub, sizeof(sub), "ボス VALKYRA の残りHP  %d%%",
                      static_cast<int>(m_bossHpRatio * 100.0f + 0.5f));
    }
    SpawnTypedText(w, "ResultSub", sub, style.left, style.SubY(), 20.0f, style.sub, kSubStart, canvas);

    // ---------------------------------------------------------
    // 4. 記録の行
    // ---------------------------------------------------------
    const int   minutes = static_cast<int>(m_playTime) / 60;
    const float seconds = m_playTime - static_cast<float>(minutes * 60);
    char        buf[kRowCount][48];
    std::snprintf(buf[0], sizeof(buf[0]), "%02d:%05.2f", minutes, seconds);
    std::snprintf(buf[1], sizeof(buf[1]), "%d回", m_hits.load());
    std::snprintf(buf[2], sizeof(buf[2]), "%d", (m_damageX10.load() + 5) / 10);
    std::snprintf(buf[3], sizeof(buf[3]), "%d回", m_justEvades.load());

    const char *labels[kRowCount] = {"経過時間", "攻撃のヒット数", "受けたダメージ", "ジャスト回避"};
    const char *names[kRowCount]  = {"ResultTime", "ResultHits", "ResultDamage", "ResultEvade"};
    for (int i = 0; i < kRowCount; ++i)
    {
        SpawnScreenRow(w, canvas, names[i], i, labels[i], buf[i], kRowStart + kRowStep * static_cast<float>(i), spawned);
    }

    // ---------------------------------------------------------
    // 5. ボタンと操作のヒント
    // ---------------------------------------------------------

    // 記録が出そろってから出す
    const float    lastRow   = static_cast<float>(kRowCount - 1);
    const float    menuTop   = style.RowY() + style.rowH * lastRow + style.rowPx + kMenuGap;
    const float    menuDelay = kRowStart + kRowStep * lastRow + kMenuDelay;
    const EntityID menu      = SpawnBox(w, "ResultMenu", {0.0f, 0.0f}, {style.left, menuTop}, {320.0f, 140.0f}, canvas);

    // 最初は「もう一度」を選んでおく
    SpawnButton(w, "BtnRetry", "LblRetry", "もう一度", {0.0f, -42.0f}, "retry", true, 230, menuDelay, menu);
    SpawnButton(w, "BtnResultTitle", "LblResultTitle", "タイトルへ", {0.0f, 43.0f}, "result_title", false, 230,
                menuDelay + 0.08f, menu);
    SpawnMenuHints(w, menu, 125.0f, spawned, 240, menuDelay + 0.16f);
}
