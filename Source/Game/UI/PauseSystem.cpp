/// @file PauseSystem.cpp
/// 役割・設計方針は PauseSystem.h を参照。

#include "PauseSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/GamePad.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/MenuUiFactory.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Time/TimeState.h" // TimeContext.isPaused（世界時計の凍結。RenderStageのVFXが尊重する）

#include <cstdint>

using namespace CCL::ECS;

namespace
{
constexpr int VKEY_ESCAPE = 0x1B; // windows.h を持ち込まないための ESC 仮想キーコード
}

void PauseSystem::Initialize()
{
    // ポーズ状態の共有リソースを用意（BaseScene が参照する）。
    if (!m_world->HasResource<GameFlowState>())
        m_world->AddResource<GameFlowState>();

    // ポーズメニューのボタン購読。
    const uint32_t kResume = CCL::Utils::HashString("resume");
    const uint32_t kQuit   = CCL::Utils::HashString("pause_quit");

    m_world->GetEventBus().Subscribe<UIButtonClickedEvent>(
        [this, kResume, kQuit](const UIButtonClickedEvent &e) {
            if (e.actionHash == kResume)
            {
                ClosePause();
            }
            else if (e.actionHash == kQuit)
            {
                if (m_transitioning) return;
                m_transitioning = true;
                // 展示会用：タイトルへは戻さず、そのままアプリを終了する。
                // ここは（ジョブスケジューラ経由の）ワーカースレッドで走りうるため、
                //    直接 PostQuitMessage を呼ぶと WM_QUIT が別スレッドのキューに積まれ、
                //    メインスレッドの PeekMessage が拾えず終了しない。
                //    → 終了要求フラグだけ立て、メインループ(Run)が拾って終了処理を行う。
                if (m_world->HasResource<GameFlowState>())
                    m_world->GetResource<GameFlowState>().quitRequested = true;
            }
        });
}

void PauseSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<Input *>()) return;
    Input *input = m_world->GetResource<Input *>();
    if (!input) return;

    const bool toggle = input->GetKeyboard().IsTriggered(VKEY_ESCAPE)
                     || input->GetGamePad().IsTriggered(GamePad::BTN_START);
    if (!toggle) return;

    // カーソル自由(UI操作)モード中は、編集中に誤ってポーズメニューが出ないよう
    //    ESC/START のポーズ切替を無視する（既にポーズ中の解除は許可）。
    if (!m_paused && m_world->HasResource<GameFlowState>()
        && m_world->GetResource<GameFlowState>().uiInteract)
    {
        return;
    }

    if (!m_paused) OpenPause();
    else          ClosePause();
}

void PauseSystem::OpenPause()
{
    using namespace CCL::GameUI;
    World &w = *m_world;

    m_paused = true;
    if (w.HasResource<GameFlowState>())
        w.GetResource<GameFlowState>().paused = true;

    // 「世界の時計」も止める。FixedUpdate が凍って TimeScaleSystem(localDt生成) が走らないため、
    // RenderStage で回り続ける VFX（GPUパーティクル等）はこのフラグを見て自前で dt=0 に凍る。
    if (w.HasResource<TimeContext>())
        w.GetResource<TimeContext>().isPaused = true;

    // オーバーレイ生成（HUDより手前へ sortOrder 高め）。
    // 出現アニメ：暗転→見出し→ボタンの順に少し遅延させてスッと出す（暗転はフェードのみ）。
    // UI階層：Canvas を親にして配下を束ねる（ヒエラルキーで入れ子表示・一括破棄）。
    m_overlay.clear();
    const EntityID canvas = SpawnCanvas(w, "PauseCanvas");
    m_overlay.push_back(canvas);

    m_overlay.push_back(SpawnPanel(w, "PauseDim", {0.0f, 0.0f}, {4000.0f, 4000.0f},
        {0.0f, 0.0f, 0.0f, 0.55f}, 200, 0.00f, 0.0f, canvas));
    m_overlay.push_back(SpawnLabel(w, "PauseTitle", "PAUSE", {-90.0f, -140.0f}, 56.0f, 220, 0.08f, canvas));

    auto resume = SpawnButton(w, "BtnResume", "LblResume", "RESUME", {0.0f, 0.0f}, "resume", true, 210, 0.16f, canvas);
    m_overlay.push_back(resume.button);
    m_overlay.push_back(resume.label);

    auto quit = SpawnButton(w, "BtnPauseQuit", "LblPauseQuit", "QUIT", {0.0f, 90.0f},
        "pause_quit", false, 210, 0.24f, canvas);
    m_overlay.push_back(quit.button);
    m_overlay.push_back(quit.label);
}

void PauseSystem::ClosePause()
{
    World &w = *m_world;

    m_paused = false;
    if (w.HasResource<GameFlowState>())
        w.GetResource<GameFlowState>().paused = false;

    // 世界の時計を再開（VFXも次フレームから通常速度に戻る）。
    if (w.HasResource<TimeContext>())
        w.GetResource<TimeContext>().isPaused = false;

    // 生成したオーバーレイ実体を破棄（ソフト削除：次の構造適用で消える）。
    for (EntityID id : m_overlay)
        w.Destroy(id);
    m_overlay.clear();
}
