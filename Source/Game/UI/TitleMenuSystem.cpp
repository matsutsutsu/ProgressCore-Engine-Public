/// @file TitleMenuSystem.cpp
/// 役割・設計方針は TitleMenuSystem.h を参照。
#include "TitleMenuSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/MenuUiFactory.h" // メニューUIの共通生成ヘルパー

#include <cstdint>

using namespace CCL::ECS;

void TitleMenuSystem::Initialize()
{
    Core::World &world = *_world;

    // --- タイトルロゴ ---
    CCL::GameUI::SpawnLabel(world, "TitleLogo", "RuneForge", {-150.0f, -180.0f}, 56.0f);

    // --- ボタン（START が初期フォーカス） ---
    CCL::GameUI::SpawnButton(world, "BtnStart", "LblStart", "START", {0.0f, -20.0f}, "start_game", true);
    CCL::GameUI::SpawnButton(world, "BtnQuit", "LblQuit", "QUIT", {0.0f, 70.0f}, "quit", false);

    // --- ボタンクリックの購読（actionHash で遷移を分岐） ---
    const uint32_t kStart = CCL::Utils::HashString("start_game");
    const uint32_t kQuit  = CCL::Utils::HashString("quit");

    world.GetEventBus().Subscribe<UIButtonClickedEvent>(
        [this, kStart, kQuit](const UIButtonClickedEvent &e) {
            if (_transitioning) return; // 遷移予約後は無視（二重遷移防止）

            if (e.actionHash == kStart)
            {
                _transitioning = true;
                // ゲーム本編へ。重い Initialize は SceneLoading が裏読みする。
                SceneManager::Instance().ChangeSceneAsync("Game");
            }
            else if (e.actionHash == kQuit)
            {
                // TODO(後続): アプリ終了要求。現状は何もしない。
            }
        });
}
