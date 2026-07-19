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

using namespace CCL::ECS;

void GameResultSystem::Initialize()
{
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

    bool playerDead   = false;
    bool anyEnemy     = false;
    int  aliveEnemies = 0;

    // 全 HealthComponent を走査し、プレイヤー／敵を仕分ける。
    ForEachWithID([&](EntityID id, const HealthComponent &hp) {
        const bool dead = (hp.currentHealth <= 0.0f) || _world->HasComponent<Tag::DeadTag>(id);

        if (_world->HasComponent<PlayerComponent>(id))
        {
            if (dead) playerDead = true; // プレイヤー死亡
        }
        else
        {
            anyEnemy = true;      // 敵が存在する
            if (!dead) ++aliveEnemies;
        }
    });

    if (anyEnemy) _sawEnemy = true; // 一度でも敵を見たら記憶

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

    // 暗転パネル（全画面を覆う大きめの半透明黒）。HUD より手前に出すため sortOrder は高め。
    SpawnPanel(w, "ResultDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.6f}, 200);

    // 見出し（勝敗で文言と色を変える。中央寄せは近似オフセット）
    const char             *title = win ? "GAME CLEAR" : "GAME OVER";
    const DirectX::XMFLOAT4 color = win ? DirectX::XMFLOAT4{1.0f, 0.9f, 0.4f, 1.0f}  // 金
                                        : DirectX::XMFLOAT4{1.0f, 0.4f, 0.4f, 1.0f}; // 赤
    EntityID titleId = SpawnLabel(w, "ResultTitle", title, {-190.0f, -140.0f}, 64.0f, 220);
    if (auto *tx = w.GetComponent<UITextComponent>(titleId)) tx->color = color;

    // ボタン（RETRY が初期フォーカス）
    SpawnButton(w, "BtnRetry", "LblRetry", "RETRY", {0.0f, 0.0f}, "retry", true, 210);
    SpawnButton(w, "BtnToTitle", "LblToTitle", "TITLE", {0.0f, 90.0f}, "result_to_title", false, 210);
}
