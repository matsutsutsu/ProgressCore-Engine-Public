#include "Game/UI/Screens/TutorialCompleteSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/Common/ScreenHeadingUi.h" // 左上の見出しと線
#include "Engine/Core/App/GameFlowState.h"
#include "Game/Logics/Tutorial/TutorialState.h"

#include <cstdint>

using namespace CCL::ECS;

namespace
{
    constexpr float kShowDelay = 1.2f;   // 教え終えてから画面を出すまでの待ち（秒・実時間）
    constexpr float kSubStart  = 0.35f;  // 線の下の1行を打ち始めるまで（秒。画面を出してから）
    constexpr float kMenuDelay = 0.6f;   // ボタンが出るまで（秒。画面を出してから）
    constexpr float kMenuGap   = 130.0f; // 見出しの下の線から、ボタンの入れ物の上端までの間隔（px）
} // namespace



void TutorialCompleteSystem::Initialize()
{
    // 決着（matchOver）を書くリソース。無ければ作る
    if (!m_world->HasResource<GameFlowState>()) m_world->AddResource<GameFlowState>();

    // ポーズのボタンも同じイベントを出すので、actionHash で見分ける
    const uint32_t kToGame = CCL::Utils::HashString("tutorial_to_game");
    const uint32_t kRetry  = CCL::Utils::HashString("tutorial_retry");
    const uint32_t kTitle  = CCL::Utils::HashString("tutorial_title");

    ListenEvent<UIButtonClickedEvent>(
        [this, kToGame, kRetry, kTitle](const UIButtonClickedEvent &e) {
            // 切り替えを頼んだ後の連打は無視する
            if (m_transitioning) return;

            // 切り替えを頼むだけにする（フェード・読み込み・片付けの時機は SceneManager が決める）
            // この購読はワーカースレッドで走ることがあるので、ここで World を片付けてはいけない
            if (e.actionHash == kToGame)
            {
                // 本番は導入ムービーから始まる。チュートリアルの続きではなく、戦いを最初から見せる
                m_transitioning = SceneManager::Instance().RequestTransition("Game", true);
            }
            else if (e.actionHash == kRetry)
            {
                m_transitioning = SceneManager::Instance().RequestTransition("Tutorial", true);
            }
            else if (e.actionHash == kTitle)
            {
                m_transitioning = SceneManager::Instance().RequestTransition("Title", true);
            }
        });
}



void TutorialCompleteSystem::Update(float dt)
{
    // 処理フロー:
    // 1. やり直されたら画面を消す
    // 2. 教え終えた瞬間に、操作とボスを止める
    // 3. 少し待ってから画面を出す

    if (!m_world->HasResource<TutorialState>()) return;
    const TutorialState &state = m_world->GetResource<TutorialState>();

    // ---------------------------------------------------------
    // 1. やり直し
    // ---------------------------------------------------------

    // デバッグ窓の「最初から開始」で、教え終えた状態から戻った
    if (!state.IsFinished())
    {
        if (m_done) CloseUI();
        return;
    }

    // ---------------------------------------------------------
    // 2. 教え終えた瞬間
    // ---------------------------------------------------------

    // 画面を出す前に止める。待っている間も操作できると、完了の画面の裏で遊べてしまう
    // （ボスは教え終えた時点で BossPhaseSystem が待機させる）
    if (!m_done)
    {
        m_done      = true;
        m_showDelay = kShowDelay;
        SetMatchOver(true);
        CCL_LOG_INFO(LogCategory::Game, "[TutorialComplete] チュートリアル完了。画面を出すのを待つ");
    }

    // ---------------------------------------------------------
    // 3. 画面を出す
    // ---------------------------------------------------------
    if (m_shown) return;

    // すぐには出さず、最後に当てた攻撃の手応えを見せてから出す
    // dt はスローの影響を受けない実時間。補助スローが残っていても、同じ秒数で出る
    m_showDelay -= dt;
    if (m_showDelay <= 0.0f)
    {
        m_shown = true;
        ShowUI();
    }
}



// 完了の画面（暗転・見出し・ボタン）を作る
void TutorialCompleteSystem::ShowUI()
{
    // 処理フロー:
    // 1. Canvas と暗転を作る
    // 2. 見出しと、線の下の1行を作る
    // 3. ボタンと操作のヒントを作る

    using namespace CCL::GameUI;
    World &w = *m_world;

    // ---------------------------------------------------------
    // 1. Canvas と暗転
    // ---------------------------------------------------------

    // 以降の UI はすべてこの Canvas の子にする
    m_overlay.clear();
    const EntityID canvas = SpawnCanvas(w, "TutorialCompleteCanvas");
    m_overlay.push_back(canvas);

    // 暗転はリザルトと同じく下ほど濃くして、左上の文字を読みやすくする
    const DirectX::XMFLOAT4 dimBottom{0.0f, 0.0f, 0.0f, 0.85f};
    m_overlay.push_back(SpawnPanel(w, "TutorialCompleteDim", {0.0f, 0.0f}, {4000.0f, 4000.0f},
                                   {0.0f, 0.0f, 0.0f, 0.55f}, 200, 0.00f, 0.0f, canvas, &dimBottom));

    // ---------------------------------------------------------
    // 2. 見出しと、線の下の1行
    // ---------------------------------------------------------

    // リザルトの勝ちと同じ白金。英語の見出しに、日本語を小さく添える
    const MenuStyleComponent style = GetMenuStyle(w);
    SpawnScreenHeading(w, canvas, "TUTORIAL COMPLETE", style.gold, m_overlay);
    m_overlay.push_back(SpawnTypedText(w, "TutorialCompleteSub", "操作の基本を覚えた", style.left, style.SubY(), 20.0f,
                                       style.sub, kSubStart, canvas));

    // ---------------------------------------------------------
    // 3. ボタンと操作のヒント
    // ---------------------------------------------------------

    // 見出しの下の線から、少し間をあけた位置に並べる（ポーズと同じ位置）
    const EntityID menu = SpawnBox(w, "TutorialCompleteMenu", {0.0f, 0.0f},
                                   {style.left, style.RuleY() + kMenuGap}, {320.0f, 225.0f}, canvas);
    m_overlay.push_back(menu);

    // 最初は「本番へ」を選んでおく。教え終えた人の多くは、そのまま本番へ進む
    const MenuButtonIds buttons[] = {
        SpawnButton(w, "BtnTutorialToGame", "LblTutorialToGame", "本番へ", {0.0f, -85.0f}, "tutorial_to_game", true,
                    230, kMenuDelay, menu),
        SpawnButton(w, "BtnTutorialRetry", "LblTutorialRetry", "もう一度", {0.0f, 0.0f}, "tutorial_retry", false, 230,
                    kMenuDelay + 0.08f, menu),
        SpawnButton(w, "BtnTutorialTitle", "LblTutorialTitle", "タイトルへ", {0.0f, 85.0f}, "tutorial_title", false, 230,
                    kMenuDelay + 0.16f, menu),
    };
    for (const MenuButtonIds &b : buttons)
    {
        m_overlay.push_back(b.button);
        m_overlay.push_back(b.label);
    }

    SpawnMenuHints(w, menu, 165.0f, m_overlay, 240, kMenuDelay + 0.24f);
}



// 完了の画面を消し、操作とボスを戻す（デバッグでやり直したとき）
void TutorialCompleteSystem::CloseUI()
{
    // Destroy は印を付けるだけで、実際に消すのはロジックの HierarchyCleanupSystem
    for (EntityID id : m_overlay)
    {
        m_world->Destroy(id);
    }
    m_overlay.clear();

    m_done  = false;
    m_shown = false;
    SetMatchOver(false);
}



void TutorialCompleteSystem::SetMatchOver(bool over)
{
    if (m_world->HasResource<GameFlowState>()) m_world->GetResource<GameFlowState>().matchOver = over;
}

// 全体登録はせず、TutorialScene が登録する
