/// @file GameResultSystem.cpp
/// 役割・設計方針は GameResultSystem.h を参照。
#include "GameResultSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/MenuUiFactory.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

#include <cstdint>
#include <imgui.h>

using namespace CCL::ECS;

void GameResultSystem::Initialize()
{
    hasGui = true; // デバッグ用の検知状態パネルを OnGui で出す

    // ボタンクリックを購読（この世界ではリザルトのボタンだけが発火源）。
    const uint32_t kRetry   = CCL::Utils::HashString("retry");
    const uint32_t kToTitle = CCL::Utils::HashString("result_to_title");

    _world->GetEventBus().Subscribe<UIButtonClickedEvent>(
        [this, kRetry, kToTitle](const UIButtonClickedEvent &e) {
            if (_transitioning) return;

            if (e.actionHash == kRetry)
            {
                // ゲーム再読込（新しい世界を裏読み）。フェードは SceneManager が統括。
                _transitioning = SceneManager::Instance().RequestTransition("Game", true);
            }
            else if (e.actionHash == kToTitle)
            {
                _transitioning = SceneManager::Instance().RequestTransition("Title", false);
            }
        });
}

void GameResultSystem::Update(float dt)
{
    if (!_debugEnabled) return; // デバッグ用の全体OFF（ステージ確認中などに勝敗判定を止める）
    if (_resultShown) return; // 既に表示済みなら何もしない

    // 勝利演出の待機中：撃破スローの余韻を見せてからリザルトを出す。
    // dt はスロー非依存の実時間なので、実時間でカウントダウンすればスロー終了後に表示される。
    if (_pendingWin)
    {
        _winDelay -= dt;
        if (_winDelay <= 0.0f)
        {
            _resultShown = true;
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
        const bool dead = (hp.currentHealth <= 0.0f) || _world->HasComponent<Tag::DeadTag>(id);

        if (_world->HasComponent<PlayerComponent>(id))
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

    if (anyEnemy) _sawEnemy = true; // 一度でも敵を見たら記憶

    // デバッグGUI用のスナップショット
    _dbgPlayerPresent = playerPresent;
    _dbgAliveEnemies  = aliveEnemies;

    // ★ セーフガード：プレイヤーが1体も存在しないシーンでは勝敗判定を行わない。
    //   ステージ実装確認用のテストScene（プレイヤー未生成）では、敵扱いの HealthComponent が
    //   生存0になった瞬間に GAME CLEAR が誤発火していた。プレイヤー不在＝プレイセッションでは
    //   ないため、ここで判定自体を打ち切る（リザルトを一切出さない）。
    if (!playerPresent) return;

    // --- 勝敗判定（敗北を優先） ---
    if (playerDead)
    {
        _resultShown = true;
        ShowResultUI(false); // GAME OVER
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Over. Result UI shown.");
    }
    else if (_sawEnemy && aliveEnemies == 0)
    {
        // 即表示せず、撃破スローの余韻を見せてから出す（実際の表示は上の待機処理で行う）。
        _pendingWin = true;
        _winDelay   = kResultDelay;
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Clear detected. Waiting for defeat slow-mo before showing result.");
    }
}

void GameResultSystem::ShowResultUI(bool win)
{
    using namespace CCL::GameUI;
    World &w = *_world;

    // 出現アニメ：要素ごとに遅延を少しずつ増やして上から順にスッと出す（暗転→見出し→ボタン）。
    // 暗転パネルはスライド無し(0)のフェードのみ、それ以外は上スライド付きで現れる。
    SpawnPanel(w, "ResultDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.6f}, 200,
        0.00f, 0.0f);

    // 見出し（勝敗で文言と色を変える。中央寄せは近似オフセット）
    const char             *title = win ? "GAME CLEAR" : "GAME OVER";
    const DirectX::XMFLOAT4 color = win ? DirectX::XMFLOAT4{1.0f, 0.9f, 0.4f, 1.0f}  // 金
                                        : DirectX::XMFLOAT4{1.0f, 0.4f, 0.4f, 1.0f}; // 赤
    EntityID titleId = SpawnLabel(w, "ResultTitle", title, {-190.0f, -140.0f}, 64.0f, 220, 0.08f);
    if (auto *tx = w.GetComponent<UITextComponent>(titleId)) tx->color = color;

    // ボタン（RETRY が初期フォーカス）
    SpawnButton(w, "BtnRetry", "LblRetry", "RETRY", {0.0f, 0.0f}, "retry", true, 210, 0.16f);
    SpawnButton(w, "BtnToTitle", "LblToTitle", "TITLE", {0.0f, 90.0f}, "result_to_title", false, 210, 0.24f);
}

// ============================================================================
//  デバッグ：勝敗検知の内部状態を可視化し、必要なら判定を止められるようにする。
// ============================================================================
void GameResultSystem::OnGui()
{
    ImGui::SeparatorText("リザルト検知 (デバッグ)");

    ImGui::Checkbox("システム有効", &_debugEnabled);
    if (!_debugEnabled)
        ImGui::TextDisabled("→ 勝敗判定・リザルト表示を停止中");

    ImGui::Separator();
    ImGui::Text("プレイヤー存在 : %s", _dbgPlayerPresent ? "yes" : "NO (テストScene扱い→抑止)");
    ImGui::Text("敵を観測(sawEnemy) : %s", _sawEnemy ? "yes" : "no");
    ImGui::Text("生存敵数 : %d", _dbgAliveEnemies);
    ImGui::Text("リザルト表示済み : %s", _resultShown ? "yes" : "no");
    ImGui::Text("勝利待機中 : %s (残り %.2fs)", _pendingWin ? "yes" : "no", _winDelay);

    ImGui::Separator();
    if (ImGui::Button("検知状態をリセット"))
    {
        _resultShown   = false;
        _sawEnemy      = false;
        _pendingWin    = false;
        _winDelay      = 0.0f;
        _transitioning = false;
    }
    ImGui::TextDisabled("※プレイヤー不在のSceneでは自動でリザルトを抑止します");
}
