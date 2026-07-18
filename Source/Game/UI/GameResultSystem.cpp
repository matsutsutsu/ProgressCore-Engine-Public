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
                _transitioning = true;
                // ゲームを再読込（新しい EditorScene を裏読み）。
                SceneManager::Instance().ChangeSceneAsync("Game");
            }
            else if (e.actionHash == kToTitle)
            {
                _transitioning = true;
                SceneManager::Instance().ChangeScene("Title");
            }
        });
}

void GameResultSystem::Update(float /*dt*/)
{
    if (_gameOver) return; // 既に表示済みなら何もしない

    bool dead = false;
    ForEachWithID([&](EntityID id, const PlayerComponent & /*p*/, const HealthComponent &hp) {
        // HP0、または死亡タグでゲームオーバー判定
        if (hp.currentHealth <= 0.0f || _world->HasComponent<Tag::DeadTag>(id))
            dead = true;
    });

    if (dead)
    {
        _gameOver = true;
        ShowResultUI();
        CCL_LOG_INFO(LogCategory::Game, "[GameResult] Game Over. Result UI shown.");
    }
}

void GameResultSystem::ShowResultUI()
{
    using namespace CCL::GameUI;
    Core::World &w = *_world;

    // 暗転パネル（全画面を覆う大きめの半透明黒）。HUD より手前に出すため sortOrder は高め。
    SpawnPanel(w, "ResultDim", {0.0f, 0.0f}, {4000.0f, 4000.0f}, {0.0f, 0.0f, 0.0f, 0.6f}, 200);

    // GAME OVER（中央やや上）
    SpawnLabel(w, "ResultTitle", "GAME OVER", {-170.0f, -140.0f}, 64.0f, 220);

    // ボタン（RETRY が初期フォーカス）
    SpawnButton(w, "BtnRetry", "LblRetry", "RETRY", {0.0f, 0.0f}, "retry", true, 210);
    SpawnButton(w, "BtnToTitle", "LblToTitle", "TITLE", {0.0f, 90.0f}, "result_to_title", false, 210);
}
