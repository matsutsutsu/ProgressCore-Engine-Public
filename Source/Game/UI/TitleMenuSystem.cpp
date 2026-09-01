/// @file TitleMenuSystem.cpp
/// 役割・設計方針は TitleMenuSystem.h を参照。

#include "TitleMenuSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/MenuUiFactory.h" // メニューUIの共通生成ヘルパー
#include "Engine/Core/App/GameFlowState.h"    // quitRequested（QUITはメインスレッドで消化する。下記参照）

#include <cstdint>

using namespace CCL::ECS;

void TitleMenuSystem::Initialize()
{
    World &world = *m_world;

    // 終了要求フラグの置き場を用意（メインスレッドの Run が毎フレーム参照する）。
    if (!world.HasResource<GameFlowState>())
        world.AddResource<GameFlowState>();

    // --- タイトルロゴ ---
    CCL::GameUI::SpawnLabel(world, "TitleLogo", "OVERCLEAVE", {-150.0f, -180.0f}, 56.0f);

    // --- ボタン（START が初期フォーカス） ---
    CCL::GameUI::SpawnButton(world, "BtnStart", "LblStart", "START", {0.0f, -20.0f}, "start_game", true);
    CCL::GameUI::SpawnButton(world, "BtnQuit", "LblQuit", "QUIT", {0.0f, 70.0f}, "quit", false);

    // --- 操作ヒント（[決定] [選択]） ---
    //   タイトルのUIはシーンごと破棄されるので、個別のIDは持たない。
    std::vector<CCL::ECS::EntityID> hints;
    CCL::GameUI::SpawnMenuHints(world, CCL::ECS::InvalidEntityID, 160.0f, hints);

    // --- ボタンクリックの購読（actionHash で遷移を分岐） ---
    const uint32_t kStart = CCL::Utils::HashString("start_game");
    const uint32_t kQuit  = CCL::Utils::HashString("quit");

    ListenEvent<UIButtonClickedEvent>(
        [this, kStart, kQuit](const UIButtonClickedEvent &e) {
            if (m_transitioning) return; // 遷移予約後は無視（二重遷移防止）

            if (e.actionHash == kStart)
            {
                // 「Game へ行きたい」と頼むだけ。フェード・ローディングの裏読み・
                // World 破棄のタイミングは SceneManager が統括する（疎結合）。
                m_transitioning = SceneManager::Instance().RequestTransition("Game", true);
            }
            else if (e.actionHash == kQuit)
            {
                m_transitioning = true;
                // この購読は UIButtonSystem 経由で発火し、ジョブスケジューラにより
                //    ワーカースレッドで走りうる。ワーカーから PostQuitMessage を呼ぶと
                //    WM_QUIT が別スレッドのキューに積まれ、メインループが拾えず終了しない。
                //    → 終了要求フラグだけ立て、メインスレッドの Run() が拾って終了する。
                if (m_world->HasResource<GameFlowState>())
                    m_world->GetResource<GameFlowState>().quitRequested = true;
            }
        });
}
