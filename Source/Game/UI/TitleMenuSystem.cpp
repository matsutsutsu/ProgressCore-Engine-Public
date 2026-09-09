// 役割・設計方針は TitleMenuSystem.h を参照。

#include "TitleMenuSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/MenuUiFactory.h" // メニューUIの共通生成ヘルパー
#include "Engine/Core/App/GameFlowState.h"    // quitRequested（QUITはメインスレッドで消化する。下記参照）

#include <cstdint>
#include <vector>

using namespace CCL::ECS;

namespace
{
// 文字とボタンは画面左に寄せる。右半分は3D背景（斬線を組む群れ）の場所で、
// ここへ文字を置くと群れと重なってどちらも読めなくなる。
constexpr float kTextLeft = -476.0f; // 「OVER」の基準X
constexpr float kIndent   = 80.0f;   // 2行目のインデント。斬線と同じ右下がりの流れを作る
constexpr float kMenuX    = -408.0f; // ボタンの基準X（ラベルの左端が文字とほぼ揃う）

// 各UIが現れるまでの待ち(秒)。0 にした要素は最初のフレームから出ている。
// 計り始めはタイトルシーンの初回 Update（＝起動直後の明転とほぼ同時）で、
// 実際に見え終わるのはここへ UIAppearComponent::duration(0.28秒) を足した頃。
//
// 以前は「群れが斬線を描き切るまで待つ」ために 3.4秒 も空けていたが、
// 群れは最初から頭上円陣が完成している設計に変えたので、待つ理由が無くなった。
// ロゴ→ボタン→ヒントの順に薄く重ねるだけの間隔にして、押せるまでを短くしている。
constexpr float kAppearLogo1  = 0.5f;
constexpr float kAppearLogo2  = 0.8f;
constexpr float kAppearStart  = 1.3f;
constexpr float kAppearQuit   = 1.45f;
constexpr float kAppearHints  = 1.7f;

// ヒント行の横移動。既定(0)だと画面中央に残り、左寄せしたメニューから離れて浮く。
constexpr float kHintShiftX = -350.0f;

// メニュー全体の寄せ。Canvas の offset は配下UIの基準点をまるごと動かすので、
// 文字・ボタン・ヒントの座標を個別に触らずに、ここ1か所で構図を決められる。
// 左下へ寄せているのは、画面の主役（頭上円陣のボスと手前のプレイヤー）が
// 上と右に居るため。UIは対角の空いた側へ逃がす。
constexpr DirectX::XMFLOAT2 kCanvasShift{-180.0f, 220.0f};

// 上を暗く落とす膜。空が明るいステージなので、これが無いと白い雲の上に白い文字が乗る。
// 下端をほぼ透明にしてあるので、床とボスの明るさはそのまま残る
// （画面全体を均一に暗くすると絵が濁るだけで、コントラストは上がらない）。
constexpr float kScrimW = 2560.0f; // 画面より大きく取り、解像度が変わっても端が出ないようにする
constexpr float kScrimH = 1440.0f;

// 下側の膜。UIを左下へ寄せたことで操作ヒントが明るい床の上に乗り、読めなくなったため足した。
// 画面全体を均一に暗くすると絵が濁るので、上下から挟んで中央（像の顔と胴）は明るく残す。
constexpr float kScrimBottomH = 760.0f;
constexpr float kScrimBottomY = 240.0f; // 膜の上端が画面 y≒520px に来る位置
} // namespace

void TitleMenuSystem::Initialize()
{
    World &world = *m_world;

    // 終了要求フラグの置き場を用意（メインスレッドの Run が毎フレーム参照する）。
    if (!world.HasResource<GameFlowState>())
        world.AddResource<GameFlowState>();

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

void TitleMenuSystem::SpawnMenu()
{
    World &world = *m_world;

    // UI階層：Canvas を1枚立てて、以降の要素は全部その子にする。
    // Canvas の矩形は常に画面全体なので、子の offset は親を付ける前と同じ値で通る。
    const EntityID canvas = CCL::GameUI::SpawnCanvas(world, "TitleCanvas", kCanvasShift);

    // --- 文字を読ませるための暗幕（最背面）---
    const DirectX::XMFLOAT4 scrimBottom{0.0f, 0.0f, 0.0f, 0.06f};
    //CCL::GameUI::SpawnPanel(world, "TitleScrim", {0.0f, 0.0f}, {kScrimW, kScrimH}, {0.0f, 0.0f, 0.02f, 0.55f}, 5, -1.0f, 0.0f, canvas, &scrimBottom);

    // --- 下端の暗幕（同じく最背面）---
    const DirectX::XMFLOAT4 scrimLowEnd{0.0f, 0.0f, 0.02f, 0.60f};
    CCL::GameUI::SpawnPanel(world, "TitleScrimBottom", {0.0f, kScrimBottomY},
        {kScrimW, kScrimBottomH}, {0.0f, 0.0f, 0.0f, 0.0f}, 5, -1.0f, 0.0f, canvas, &scrimLowEnd);

    // --- タイトルロゴ ---
    // 「OVER」「CLEAVE」を2行に割り、斬線（3D側の群れ）が2語を裂いている構図にする。
    CCL::GameUI::SpawnLabel(world, "TitleLogoOver", "OVER", {kTextLeft, -276.0f}, 88.0f, 11,
        kAppearLogo1, canvas);
    CCL::GameUI::SpawnLabel(world, "TitleLogoCleave", "CLEAVE", {kTextLeft + kIndent, -176.0f},
        88.0f, 11, kAppearLogo2, canvas);

    // --- ボタン（START が初期フォーカス） ---
    CCL::GameUI::SpawnButton(world, "BtnStart", "LblStart", "START", {kMenuX, 40.0f}, "start_game",
        true, 10, kAppearStart, canvas);
    CCL::GameUI::SpawnButton(world, "BtnQuit", "LblQuit", "QUIT", {kMenuX, 125.0f}, "quit", false,
        10, kAppearQuit, canvas);

    // --- 操作ヒント（[決定] [選択]） ---
    //   タイトルのUIはシーンごと破棄されるので、個別のIDは持たない。
    std::vector<CCL::ECS::EntityID> hints;
    CCL::GameUI::SpawnMenuHints(world, canvas, 205.0f, hints, 230, kAppearHints, kHintShiftX);
}

void TitleMenuSystem::Update(float /*dt*/)
{
    if (m_menuSpawned) return; // 以降はイベント駆動なので毎フレーム処理は不要

    SpawnMenu();
    m_menuSpawned = true;
}
