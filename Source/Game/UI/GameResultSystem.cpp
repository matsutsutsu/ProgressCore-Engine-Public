#include "GameResultSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/MenuUiFactory.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

#include <cstdint>

#include <imgui.h>

using namespace CCL::ECS;

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
}

void GameResultSystem::Update(float dt)
{
    if (!m_debugEnabled) return; // ステージを確かめている間などに判定を止める（デバッグ用）
    if (m_resultShown) return;

    // 勝った後の待ち。dt はスローの影響を受けない実時間なので、数え終わればスローも終わっている
    if (m_pendingWin)
    {
        m_winDelay -= dt;
        if (m_winDelay <= 0.0f)
        {
            m_resultShown = true;
            ShowResultUI(true); // GAME CLEAR
            CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear. Result UI shown (after defeat slow-mo).");
        }
        return; // 待っている間は判定し直さない
    }

    bool playerPresent = false;
    bool playerDead    = false;
    bool anyEnemy      = false;
    int  aliveEnemies  = 0;

    // HealthComponent を持つエンティティを、プレイヤーと敵に分けて数える
    ForEachWithID([&](EntityID id, const HealthComponent &hp) {
        const bool dead = (hp.currentHealth <= 0.0f) || m_world->HasComponent<Tag::DeadTag>(id);

        if (m_world->HasComponent<PlayerComponent>(id))
        {
            playerPresent = true;
            if (dead) playerDead = true;
        }
        else
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

    // 負けを先に判定する
    if (playerDead)
    {
        m_resultShown = true;
        ShowResultUI(false); // GAME OVER
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

void GameResultSystem::ShowResultUI(bool win)
{
    using namespace CCL::GameUI;
    World &w = *m_world;

    // 以降の UI はすべてこの Canvas の子にする
    const EntityID canvas = SpawnCanvas(w, "ResultCanvas");

    // 暗転 → 見出し → ボタンの順に、出始めを少しずつ遅らせる。
    // 暗転はフェードだけ（スライド 0）、それ以外は上から少しスライドして出る
    SpawnPanel(w, "ResultDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.6f}, 200,
               0.00f, 0.0f, canvas);

    // 見出し（勝敗で文言と色を変える。中央に見えるよう offset で寄せている）
    const char             *title   = win ? "GAME CLEAR" : "GAME OVER";
    const DirectX::XMFLOAT4 color   = win ? DirectX::XMFLOAT4{1.0f, 0.9f, 0.4f, 1.0f}
                                          : DirectX::XMFLOAT4{1.0f, 0.4f, 0.4f, 1.0f};
    EntityID                titleId = SpawnLabel(w, "ResultTitle", title, {-190.0f, -140.0f}, 64.0f, 220, 0.08f, canvas);
    if (auto *tx = w.GetComponent<UITextComponent>(titleId)) tx->color = color;

    // ボタン（最初は RETRY を選んでおく）
    SpawnButton(w, "BtnRetry", "LblRetry", "RETRY", {0.0f, 0.0f}, "retry", true, 210, 0.16f, canvas);
    SpawnButton(w, "BtnResultTitle", "LblResultTitle", "TITLE", {0.0f, 90.0f}, "result_title",
                false, 210, 0.24f, canvas);

    // 操作のヒント（決定・選択）。シーンを切り替えるときに消えるので、個別の ID は持たない
    std::vector<EntityID> hints;
    SpawnMenuHints(w, canvas, 175.0f, hints, 230, 0.32f);
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

    ImGui::Separator();
    if (ImGui::Button("検知状態をリセット"))
    {
        m_resultShown   = false;
        m_sawEnemy      = false;
        m_pendingWin    = false;
        m_winDelay      = 0.0f;
        m_transitioning = false;
    }
    ImGui::TextDisabled("※プレイヤー不在のSceneでは自動でリザルトを抑止します");
}
