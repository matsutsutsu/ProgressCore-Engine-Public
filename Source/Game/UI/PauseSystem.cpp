#include "PauseSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/GamePad.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Engine/UI/ECS/GameViewportInfo.h"
#include "Game/UI/MenuUiFactory.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Time/TimeService.h"

#include <cstdint>

using namespace CCL::ECS;

namespace
{
    constexpr int VKEY_ESCAPE = 0x1B; // windows.h を持ち込まないための ESC 仮想キーコード
}

void PauseSystem::Initialize()
{
    // 一時停止の状態を置くリソース（BaseScene が見る）
    if (!m_world->HasResource<GameFlowState>())
        m_world->AddResource<GameFlowState>();

    // ポーズメニューのボタンを購読する
    const uint32_t kResume = CCL::Utils::HashString("resume");
    const uint32_t kTitle  = CCL::Utils::HashString("pause_title");

    ListenEvent<UIButtonClickedEvent>(
        [this, kResume, kTitle](const UIButtonClickedEvent &e) {
            if (e.actionHash == kResume)
            {
                ClosePause();
            }
            else if (e.actionHash == kTitle)
            {
                if (m_transitioning) return; // 切り替えを頼んだ後は無視する

                // タイトルへの切り替えを頼むだけ。フェード・裏での読み込み・World の片付けの順番は
                // SceneManager が決める。この購読はワーカースレッドで走ることがあるので、
                // ここで World を片付けてはいけない。
                // 一時停止は解除しない。解除すると、暗転している間にゲームが数百ms進んでしまう。
                // 時間も GameFlowState も World ごとにあるので、切り替え先には残らない
                m_transitioning = SceneManager::Instance().RequestTransition("Title", true);
            }
        });
}

void PauseSystem::Update(float /*dt*/)
{
    if (!m_world->HasResource<Input *>()) return;
    Input *input = m_world->GetResource<Input *>();
    if (!input) return;

    // エディタの別のウィンドウを操作している間と、UI 操作モード（F2）の間は Esc を受けない（GameViewportInfo.h）。
    // エディタで押した Esc で一時停止が切り替わらないようにするため。パッドの START は止めない
    const bool keyUsable = !m_world->HasResource<GameViewportInfo>() ||
                           m_world->GetResource<GameViewportInfo>().KeyboardToGame();
    const bool toggle    = (keyUsable && input->GetKeyboard().IsTriggered(VKEY_ESCAPE)) ||
                        input->GetGamePad().IsTriggered(GamePad::BTN_START);
    if (!toggle) return;

    // UI 操作モード（カーソルを自由に動かすモード）の間は、編集中にポーズメニューが出ないよう
    // 一時停止しない（一時停止中の解除はできる）
    if (!m_paused && m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().uiInteract)
    {
        return;
    }

    if (!m_paused)
        OpenPause();
    else
        ClosePause();
}

void PauseSystem::OpenPause()
{
    using namespace CCL::GameUI;
    World &w = *m_world;

    m_paused = true;
    if (w.HasResource<GameFlowState>())
        w.GetResource<GameFlowState>().paused = true;

    // ゲームの時間も止める。FixedUpdate が止まると TimeScaleSystem（localDt を作る）も走らないので、
    // 描画ステージで動き続ける VFX（GPU パーティクルなど）は、このフラグを見て自分で dt を 0 にする
    Time::Pause(w);

    // メニューを作る（HUD より手前に出すため sortOrder を高くする）。
    // 暗転 → 見出し → ボタンの順に、出始めを少しずつ遅らせる（暗転はフェードだけ）。
    // すべて Canvas の子にする
    m_overlay.clear();
    const EntityID canvas = SpawnCanvas(w, "PauseCanvas");
    m_overlay.push_back(canvas);

    // 暗転は上下で濃さを変える。一色の黒は作りかけの板のように見えやすい。
    // 上の方を薄くすると後ろのゲーム画面が見え、止まっていることが分かる
    static const DirectX::XMFLOAT4 kDimBottom = {0.02f, 0.02f, 0.05f, 0.78f};
    m_overlay.push_back(SpawnPanel(w, "PauseDim", {0.0f, 0.0f}, {4000.0f, 4000.0f},
                                   {0.0f, 0.0f, 0.0f, 0.45f}, 200, 0.00f, 0.0f, canvas, &kDimBottom));
    m_overlay.push_back(SpawnLabel(w, "PauseTitle", "PAUSE", {-90.0f, -140.0f}, 56.0f, 220, 0.08f, canvas));

    auto resume = SpawnButton(w, "BtnResume", "LblResume", "RESUME", {0.0f, 0.0f}, "resume", true, 210, 0.16f, canvas);
    m_overlay.push_back(resume.button);
    m_overlay.push_back(resume.label);

    // ここからはタイトルへ戻れるだけにする。アプリの終了はタイトル画面の QUIT だけ
    // （ゲーム中からすぐ終了できると、押し間違えたときに戻れない）
    auto toTitle = SpawnButton(w, "BtnPauseTitle", "LblPauseTitle", "TITLE", {0.0f, 90.0f},
                               "pause_title", false, 210, 0.24f, canvas);
    m_overlay.push_back(toTitle.button);
    m_overlay.push_back(toTitle.label);

    // 操作のヒント（決定・選択）。ボタンより少し遅れて出す
    SpawnMenuHints(w, canvas, 175.0f, m_overlay, 230, 0.32f);
}

void PauseSystem::ClosePause()
{
    World &w = *m_world;

    m_paused = false;
    if (w.HasResource<GameFlowState>())
        w.GetResource<GameFlowState>().paused = false;

    // ゲームの時間を再開する（VFX も次のフレームから元の速さに戻る）
    Time::Resume(w);

    // 作ったメニューを消す（実際に消えるのは、次に構造の変更を反映したとき）
    for (EntityID id : m_overlay)
        w.Destroy(id);
    m_overlay.clear();
}
