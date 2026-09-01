// 役割・設計方針は GameResultSystem.h を参照。

#include "GameResultSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/MenuUiFactory.h"
#include "Engine/Core/App/GameFlowState.h" // quitRequested（QUITはメインスレッドで消化する。下記参照）
#include "Game/Logics/Combat/DeadTag.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

#include <cstdint>

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;

void GameResultSystem::Initialize()
{
    // 終了要求フラグの置き場を用意（メインスレッドの Run が毎フレーム参照する）。
    if (!m_world->HasResource<GameFlowState>())
        m_world->AddResource<GameFlowState>();

    // ボタンクリックを購読（この世界ではリザルトのボタンだけが発火源）。
    const uint32_t kRetry = CCL::Utils::HashString("retry");
    const uint32_t kQuit  = CCL::Utils::HashString("result_quit");

    ListenEvent<UIButtonClickedEvent>(
        [this, kRetry, kQuit](const UIButtonClickedEvent &e) {
            if (m_transitioning) return;

            if (e.actionHash == kRetry)
            {
                // ゲーム再読込（新しい世界を裏読み）。フェードは SceneManager が統括。
                m_transitioning = SceneManager::Instance().RequestTransition("Game", true);
            }
            else if (e.actionHash == kQuit)
            {
                m_transitioning = true;
                // 展示会用：タイトルへは戻さず、そのままアプリを終了する。
                // この購読はワーカースレッドで走りうるため、直接 PostQuitMessage を
                //    呼ぶと WM_QUIT が別スレッドに積まれメインループが拾えない。
                //    → フラグだけ立て、メインスレッドの Run() が拾って終了する。
                if (m_world->HasResource<GameFlowState>())
                    m_world->GetResource<GameFlowState>().quitRequested = true;
            }
        });
}

void GameResultSystem::Update(float dt)
{
    if (!m_debugEnabled) return; // デバッグ用の全体OFF（ステージ確認中などに勝敗判定を止める）
    if (m_resultShown) return; // 既に表示済みなら何もしない

    // 勝利演出の待機中：撃破スローの余韻を見せてからリザルトを出す。
    // dt はスロー非依存の実時間なので、実時間でカウントダウンすればスロー終了後に表示される。
    if (m_pendingWin)
    {
        m_winDelay -= dt;
        if (m_winDelay <= 0.0f)
        {
            m_resultShown = true;
            ShowResultUI(true); // GAME CLEAR
            CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear. Result UI shown (after defeat slow-mo).");
        }
        return; // 待機中は勝敗の再判定をしない
    }

    bool playerPresent = false;
    bool playerDead    = false;
    bool anyEnemy      = false;
    int  aliveEnemies  = 0;

    // 全 HealthComponent を走査し、プレイヤー／敵を仕分ける。
    ForEachWithID([&](EntityID id, const HealthComponent &hp) {
        const bool dead = (hp.currentHealth <= 0.0f) || m_world->HasComponent<Tag::DeadTag>(id);

        if (m_world->HasComponent<PlayerComponent>(id))
        {
            playerPresent = true;        // プレイヤーが世界に存在する
            if (dead) playerDead = true; // プレイヤー死亡
        }
        else
        {
            anyEnemy = true;      // 敵が存在する
            if (!dead) ++aliveEnemies;
        }
    });

    if (anyEnemy) m_sawEnemy = true; // 一度でも敵を見たら記憶

    // デバッグGUI用のスナップショット
    m_dbgPlayerPresent = playerPresent;
    m_dbgAliveEnemies  = aliveEnemies;

    // セーフガード：プレイヤーが1体も存在しないシーンでは勝敗判定を行わない。
    //   ステージ実装確認用のテストScene（プレイヤー未生成）では、敵扱いの HealthComponent が
    //   生存0になった瞬間に GAME CLEAR が誤発火していた。プレイヤー不在＝プレイセッションでは
    //   ないため、ここで判定自体を打ち切る（リザルトを一切出さない）。
    if (!playerPresent) return;

    // --- 勝敗判定（敗北を優先） ---
    if (playerDead)
    {
        m_resultShown = true;
        ShowResultUI(false); // GAME OVER
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Over. Result UI shown.");
    }
    else if (m_sawEnemy && aliveEnemies == 0)
    {
        // 即表示せず、撃破スローの余韻を見せてから出す（実際の表示は上の待機処理で行う）。
        m_pendingWin = true;
        m_winDelay   = kResultDelay;
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear detected. Waiting for defeat slow-mo before showing result.");
    }
}

void GameResultSystem::ShowResultUI(bool win)
{
    using namespace CCL::GameUI;
    World &w = *m_world;

    // UI階層：Canvas を親にして配下を束ねる（ヒエラルキーで入れ子表示）。
    const EntityID canvas = SpawnCanvas(w, "ResultCanvas");

    // 出現アニメ：要素ごとに遅延を少しずつ増やして上から順にスッと出す（暗転→見出し→ボタン）。
    // 暗転パネルはスライド無し(0)のフェードのみ、それ以外は上スライド付きで現れる。
    SpawnPanel(w, "ResultDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.6f}, 200,
        0.00f, 0.0f, canvas);

    // 見出し（勝敗で文言と色を変える。中央寄せは近似オフセット）
    const char             *title = win ? "GAME CLEAR" : "GAME OVER";
    const DirectX::XMFLOAT4 color = win ? DirectX::XMFLOAT4{1.0f, 0.9f, 0.4f, 1.0f}  // 金
                                        : DirectX::XMFLOAT4{1.0f, 0.4f, 0.4f, 1.0f}; // 赤
    EntityID titleId = SpawnLabel(w, "ResultTitle", title, {-190.0f, -140.0f}, 64.0f, 220, 0.08f, canvas);
    if (auto *tx = w.GetComponent<UITextComponent>(titleId)) tx->color = color;

    // ボタン（RETRY が初期フォーカス）
    SpawnButton(w, "BtnRetry", "LblRetry", "RETRY", {0.0f, 0.0f}, "retry", true, 210, 0.16f, canvas);
    SpawnButton(w, "BtnQuit", "LblQuit", "QUIT", {0.0f, 90.0f}, "result_quit", false, 210, 0.24f, canvas);

    // 操作ヒント（[決定] [選択]）。Canvas の子なので、この画面を畳むときに一緒に消える。
    //   ここでは受け皿だけ用意する（Canvas ごと破棄するため個別のIDは持たない）。
    std::vector<EntityID> hints;
    SpawnMenuHints(w, canvas, 175.0f, hints, 230, 0.32f);
}

// ============================================================================
//  デバッグ：勝敗検知の内部状態を可視化し、必要なら判定を止められるようにする。
//  ※ImGuiはエディタ専用。Shipping(Release)ビルドではスタブに差し替える。
// ============================================================================
#ifdef ENABLE_EDITOR
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
#else
void GameResultSystem::OnGui() {}
#endif
