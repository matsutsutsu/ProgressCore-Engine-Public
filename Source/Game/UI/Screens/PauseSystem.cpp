#include "Game/UI/Screens/PauseSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/Platform/Input/Input.h"
#include "Engine/Platform/Input/GamePad.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Engine/UI/ECS/GameViewportInfo.h"
#include "Game/UI/Common/ScreenHeadingUi.h" // 左上の見出しと線（リザルトと共通の組み）
#include "Game/UI/Screens/SettingsScreenSystem.h" // SettingsScreenState（設定画面を開いている間は Esc を受けない）
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Time/TimeService.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h" // SetParent（親の子のリストに繋ぐ）

#include <cstdint>

using namespace CCL::ECS;

namespace
{
    constexpr int VKEY_ESCAPE = 0x1B; // windows.h を持ち込まないための ESC 仮想キーコード

    constexpr float kMenuDelay = 0.15f;  // ボタンが出るまでの時間（秒）。すぐ操作したい画面なので短くする
    constexpr float kMenuGap   = 130.0f; // 見出しの下の線から、ボタンの入れ物の上端までの間隔（px）
}

void PauseSystem::Initialize()
{
    // 一時停止の状態を置くリソース（BaseScene が見る）
    if (!m_world->HasResource<GameFlowState>())
    {
        m_world->AddResource<GameFlowState>();
    }

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
                // 切り替えを頼んだ後の連打は無視する。
                if (m_transitioning)
                {
                    return;
                }

                // タイトルへの切り替えを頼むだけにする。フェード・裏での読み込み・World の片付けの順番は SceneManager が決める。
                // この購読はワーカースレッドで走ることがあるので、ここで World を片付けてはいけない。

                // 一時停止は解除しない。解除すると、暗転している間にゲームが数百ミリ秒進んでしまう。
                // 時間も GameFlowState も World ごとにあるので、切り替え先には残らない。
                m_transitioning = SceneManager::Instance().RequestTransition("Title", true);
            }
        });
}

std::vector<TypeID> PauseSystem::GetWriteTypes() const
{
    return {TypeInfo<TransformComponent>::ID()};
}

void PauseSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. 作ったメニューを、親の子のリストに繋ぐ
    // 2. アプリが裏に回っていたら、自動で一時停止する（製品ビルドだけ）
    // 3. 一時停止の切り替えの入力を読む
    // 4. 切り替えてよい状況かを確かめる
    // 5. 一時停止を切り替える

    // ---------------------------------------------------------
    // 1. 親子のリスト
    // ---------------------------------------------------------

    // 生成はメニューを作った次のフレームに反映されるので、それを待って繋ぐ
    if (m_linkPending && LinkOverlay())
    {
        m_linkPending = false;
    }

    if (!m_world->HasResource<Input *>())
    {
        return;
    }
    Input *input = m_world->GetResource<Input *>();
    if (!input)
    {
        return;
    }

    // ---------------------------------------------------------
    // 2. 自動の一時停止
    // ---------------------------------------------------------
#ifdef SHIPPING_BUILD
    // 前面に戻っても自動では再開しない。戻った瞬間に攻撃を受けないようにする。

    // 開発中は VS などへ頻繁に切り替えて邪魔になるので、製品ビルドだけで行う。

    // 決着後（リザルト）・シーンの切り替え中・UI 操作モードの間は開かない。
    if (!m_paused && !input->IsAppActive() && !m_transitioning && !SceneManager::Instance().IsTransitioning())
    {
        const GameFlowState *flow = m_world->HasResource<GameFlowState>() ? &m_world->GetResource<GameFlowState>() : nullptr;
        if (!flow || (!flow->matchOver && !flow->uiInteract))
        {
            OpenPause();
            return;
        }
    }
#endif

    // ---------------------------------------------------------
    // 3. 入力
    // ---------------------------------------------------------

    // エディタの別のウィンドウを操作している間と、UI 操作モード（F2）の間は Esc を受けない（GameViewportInfo.h）。
    // エディタで押した Esc で、一時停止が切り替わらないようにする。パッドの START は止めない。
    const bool keyUsable = !m_world->HasResource<GameViewportInfo>() ||
                           m_world->GetResource<GameViewportInfo>().KeyboardToGame();
    const bool toggle    = (keyUsable && input->GetKeyboard().IsTriggered(VKEY_ESCAPE)) ||
                        input->GetGamePad().IsTriggered(GamePad::BTN_START);
    if (!toggle)
    {
        return;
    }

    // ---------------------------------------------------------
    // 4. 切り替えてよいか
    // ---------------------------------------------------------

    // 設定画面を開いている間の Esc・START は、設定画面を閉じるためのもの。ポーズは切り替えない。
    if (m_world->HasResource<SettingsScreenState>() && m_world->GetResource<SettingsScreenState>().open)
    {
        return;
    }

    // UI 操作モード（カーソルを自由に動かすモード）の間は、編集中にポーズメニューが出ないよう一時停止しない。
    // 一時停止中の解除はできる。
    if (!m_paused && m_world->HasResource<GameFlowState>() && m_world->GetResource<GameFlowState>().uiInteract)
    {
        return;
    }

    // ---------------------------------------------------------
    // 5. 切り替え
    // ---------------------------------------------------------
    if (!m_paused)
    {
        OpenPause();
    }
    else
    {
        ClosePause();
    }
}

void PauseSystem::OpenPause()
{
    // 処理フロー:
    // 1. 一時停止の状態にし、ゲームの時間を止める
    // 2. Canvas と暗転を作る
    // 3. 見出しを作る
    // 4. ボタンと操作のヒントを作る

    using namespace CCL::GameUI;
    World &w = *m_world;

    // ---------------------------------------------------------
    // 1. 一時停止
    // ---------------------------------------------------------
    m_paused = true;
    if (w.HasResource<GameFlowState>())
    {
        w.GetResource<GameFlowState>().paused = true;
    }

    // ゲームの時間も止める。
    // FixedUpdate が止まると TimeScaleSystem（localDt を作る）も走らない。
    // そのため、描画ステージで動き続ける VFX（GPU パーティクルなど）は、このフラグを見て自分で dt を 0 にする。
    Time::Pause(w);

    // ---------------------------------------------------------
    // 2. Canvas と暗転
    // ---------------------------------------------------------

    // メニューはすべて Canvas の子にし、閉じるときにまとめて消す。
    // 描画順は HUD より手前にする。
    m_overlay.clear();
    const EntityID canvas = SpawnCanvas(w, "PauseCanvas");
    m_overlay.push_back(canvas);

    // 暗転は上下で濃さを変える。一色の黒は作りかけの板のように見えやすい。
    // 上の方を薄くすると後ろのゲーム画面が見え、止まっていることが分かる。
    static const DirectX::XMFLOAT4 kDimBottom = {0.02f, 0.02f, 0.05f, 0.82f};
    m_overlay.push_back(SpawnPanel(w, "PauseDim", {0.0f, 0.0f}, {4000.0f, 4000.0f},
                                   {0.0f, 0.0f, 0.0f, 0.5f}, 200, 0.00f, 0.0f, canvas, &kDimBottom));

    // ---------------------------------------------------------
    // 3. 見出し
    // ---------------------------------------------------------

    // 見出しと線だけを出す。ポーズ中に読ませる情報は置かず、すぐ戻れることを優先する。
    // 英語の見出しに、日本語を小さく添える。
    SpawnScreenHeading(w, canvas, "PAUSE", ScreenColor::kGold, m_overlay);
    m_overlay.push_back(SpawnTypedText(w, "PauseSub", "一時停止", ScreenLayout::kLeft, ScreenLayout::kSubY, 20.0f,
                                       ScreenColor::kSub, 0.2f, canvas));

    // ---------------------------------------------------------
    // 4. ボタンと操作のヒント
    // ---------------------------------------------------------

    // 見出しの下の線から、少し間をあけた位置に並べる。
    const EntityID menu = SpawnBox(w, "PauseMenu", {0.0f, 0.0f}, {ScreenLayout::kLeft, ScreenLayout::kRuleY + kMenuGap},
                                   {320.0f, 225.0f}, canvas);
    m_overlay.push_back(menu);

    auto resume = SpawnButton(w, "BtnResume", "LblResume", "ゲームに戻る", {0.0f, -85.0f}, "resume", true, 230, kMenuDelay, menu);
    m_overlay.push_back(resume.button);
    m_overlay.push_back(resume.label);

    // 「設定」は SettingsScreenSystem が "open_settings" のボタンを見て開く。
    auto settings = SpawnButton(w, "BtnPauseSettings", "LblPauseSettings", "設定", {0.0f, 0.0f}, "open_settings", false,
                                230, kMenuDelay + 0.03f, menu);
    m_overlay.push_back(settings.button);
    m_overlay.push_back(settings.label);

    // ここからはタイトルへ戻れるだけにする。アプリの終了は、タイトル画面の「終了」だけにする。
    // ゲーム中からすぐ終了できると、押し間違えたときに戻れない。
    auto toTitle = SpawnButton(w, "BtnPauseTitle", "LblPauseTitle", "タイトルへ", {0.0f, 85.0f}, "pause_title", false, 230,
                               kMenuDelay + 0.06f, menu);
    m_overlay.push_back(toTitle.button);
    m_overlay.push_back(toTitle.label);

    // 操作のヒント（決定・選択）は、ボタンより少し遅れて出す。
    SpawnMenuHints(w, menu, 165.0f, m_overlay, 240, kMenuDelay + 0.12f);

    // 親の子のリストへは、生成が反映される次のフレームに繋ぐ（Update）
    m_linkPending = true;
}

bool PauseSystem::LinkOverlay()
{
    World &w = *m_world;

    // 1つでもまだ引けなければ、生成が反映されていない。全部そろってから繋ぐ
    for (EntityID id : m_overlay)
    {
        if (!w.GetComponent<TransformComponent>(id))
        {
            return false;
        }
    }

    // 作ったときに子から親を指す parent だけは入っているので、それを使って親の側からも繋ぐ。
    // 再開した後に TransformUpdateSystem が同じ繋ぎ直しを見ても、繋がっているので何もしない
    for (EntityID id : m_overlay)
    {
        const EntityID parent = w.GetComponent<TransformComponent>(id)->parent;
        if (parent != 0)
        {
            TransformUpdateSystem::SetParent(w, id, parent);
        }
    }
    return true;
}

void PauseSystem::ClosePause()
{
    World &w = *m_world;

    m_paused = false;
    if (w.HasResource<GameFlowState>())
    {
        w.GetResource<GameFlowState>().paused = false;
    }

    // ゲームの時間を再開する。VFX も次のフレームから元の速さに戻る。
    Time::Resume(w);

    // 作ったメニューを消す。
    // Destroy は印を付けるだけで、実際に消すのはロジックの HierarchyCleanupSystem。
    // ここで一時停止を解除しているので、次のロジックの更新で消える。
    for (EntityID id : m_overlay)
    {
        w.Destroy(id);
    }
    m_overlay.clear();
    m_linkPending = false;
}
