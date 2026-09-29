#include "TitleMenuSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/MenuUiFactory.h"
#include "Engine/Core/App/GameFlowState.h"

#include <cstdint>
#include <vector>

using namespace CCL::ECS;

namespace
{
    // 文字とボタンは画面の左に寄せる。右側は 3D の背景（ボスと群れ）が見える所で、
    // 文字を置くと重なってどちらも見えにくくなる
    constexpr float kTextLeft = -476.0f; // 「OVER」の X
    constexpr float kIndent   = 80.0f;   // 2行目を右へずらす量。右下がりに並べる
    constexpr float kMenuX    = -408.0f; // ボタンの X（ラベルの左端が文字の左端とほぼそろう）

    // 各 UI が出始めるまでの待ち時間（秒）。数え始めはタイトルの最初の Update（起動して明るくなるのとほぼ同時）で、
    // 見え終わるのはこれに UIAppearComponent::duration（0.28 秒）を足した頃。
    // 群れは最初から輪ができているので待つ必要はない。ロゴ → ボタン → ヒントの順に少しずつ重なる間隔にして、
    // ボタンを押せるまでを短くしている
    constexpr float kAppearLogo1 = 0.5f;
    constexpr float kAppearLogo2 = 0.8f;
    constexpr float kAppearStart = 1.3f;
    constexpr float kAppearQuit  = 1.45f;
    constexpr float kAppearHints = 1.7f;

    // ヒントの行を横に動かす量。0 だと画面の中央に残り、左に寄せたメニューから離れてしまう
    constexpr float kHintShiftX = -350.0f;

    // メニュー全体を動かす量。Canvas の offset は子の UI をまとめて動かすので、
    // 文字・ボタン・ヒントの座標を1つずつ変えずに、ここだけで位置を決められる。
    // 左下へ寄せるのは、ボスと群れと手前のプレイヤーが画面の上と右にいるため
    constexpr DirectX::XMFLOAT2 kCanvasShift{-180.0f, 220.0f};

    // 画面の下を暗くする半透明のパネル。UI を左下に寄せているので、これが無いと操作のヒントが
    // 明るい床に重なって読めない。上端を透明にしたグラデーションにして、像の顔と胴は明るく残す
    // （画面全体を暗くすると絵が濁るだけで、文字は読みやすくならない）
    constexpr float kScrimW       = 2560.0f; // 画面より大きくして、解像度が変わっても端が見えないようにする
    constexpr float kScrimBottomH = 760.0f;
    constexpr float kScrimBottomY = 240.0f; // パネルの上端が画面の y ≒ 520px に来る位置
} // namespace

void TitleMenuSystem::Initialize()
{
    World &world = *m_world;

    // 終了の要求を置くリソース（メインスレッドの Run が毎フレーム見る）
    if (!world.HasResource<GameFlowState>())
        world.AddResource<GameFlowState>();

    // ボタンのクリックを購読し、actionHash で分ける
    const uint32_t kStart = CCL::Utils::HashString("start_game");
    const uint32_t kQuit  = CCL::Utils::HashString("quit");

    ListenEvent<UIButtonClickedEvent>(
        [this, kStart, kQuit](const UIButtonClickedEvent &e) {
            if (m_transitioning) return; // 切り替えを頼んだ後は無視する

            if (e.actionHash == kStart)
            {
                // ゲームへの切り替えを頼むだけ。フェード・裏での読み込み・World の片付けの時機は SceneManager が決める
                m_transitioning = SceneManager::Instance().RequestTransition("Game", true);
            }
            else if (e.actionHash == kQuit)
            {
                m_transitioning = true;
                // この購読は UIButtonSystem から呼ばれ、ワーカースレッドで走ることがある。
                // ワーカーから PostQuitMessage を呼ぶと WM_QUIT がそのスレッドのキューに入り、
                // メインループが受け取れず終了しない。そこでフラグだけ立て、メインスレッドの Run() が見て終了する
                if (m_world->HasResource<GameFlowState>())
                    m_world->GetResource<GameFlowState>().quitRequested = true;
            }
        });
}

void TitleMenuSystem::SpawnMenu()
{
    World &world = *m_world;

    // 以降の UI はすべてこの Canvas の子にする。Canvas の矩形は常に画面全体なので、
    // 子の offset は画面を基準にした値のまま使える
    const EntityID canvas = CCL::GameUI::SpawnCanvas(world, "TitleCanvas", kCanvasShift);

    // --- 画面の下を暗くするパネル（一番奥） ---
    const DirectX::XMFLOAT4 scrimLowEnd{0.0f, 0.0f, 0.02f, 0.60f};
    CCL::GameUI::SpawnPanel(world, "TitleScrimBottom", {0.0f, kScrimBottomY},
                            {kScrimW, kScrimBottomH}, {0.0f, 0.0f, 0.0f, 0.0f}, 5, -1.0f, 0.0f, canvas, &scrimLowEnd);

    // --- タイトルの文字（「OVER」と「CLEAVE」の2行） ---
    CCL::GameUI::SpawnLabel(world, "TitleLogoOver", "OVER", {kTextLeft, -276.0f}, 88.0f, 11,
                            kAppearLogo1, canvas);
    CCL::GameUI::SpawnLabel(world, "TitleLogoCleave", "CLEAVE", {kTextLeft + kIndent, -176.0f},
                            88.0f, 11, kAppearLogo2, canvas);

    // --- ボタン（最初は START を選んでおく） ---
    CCL::GameUI::SpawnButton(world, "BtnStart", "LblStart", "START", {kMenuX, 40.0f}, "start_game",
                             true, 10, kAppearStart, canvas);
    CCL::GameUI::SpawnButton(world, "BtnQuit", "LblQuit", "QUIT", {kMenuX, 125.0f}, "quit", false,
                             10, kAppearQuit, canvas);

    // --- 操作のヒント（決定・選択） ---
    // タイトルの UI はシーンごと消えるので、個別の ID は持たない
    std::vector<CCL::ECS::EntityID> hints;
    CCL::GameUI::SpawnMenuHints(world, canvas, 205.0f, hints, 230, kAppearHints, kHintShiftX);
}

void TitleMenuSystem::Update(float /*dt*/)
{
    if (m_menuSpawned) return; // 作った後はボタンのイベントで動くので、毎フレームの処理は無い

    SpawnMenu();
    m_menuSpawned = true;
}
