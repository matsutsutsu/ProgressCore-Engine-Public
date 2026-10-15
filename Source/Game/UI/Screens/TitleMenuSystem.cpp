#include "Game/UI/Screens/TitleMenuSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Core/Math/StringHash.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Game/UI/Common/MenuUiFactory.h"
#include "Engine/Core/App/GameFlowState.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace CCL::ECS;

namespace
{
    // ロゴとボタンは画面の左に寄せる。
    // 右側は 3D の背景（ボスと群れ）が見える所で、文字を置くと重なってどちらも見えにくくなる。
    constexpr float kMenuX = -408.0f; // ボタンの X（画面の中央から、px）

    // --- タイトルロゴ（位置は画面の中央から、px） ---
    constexpr const char *kLogoPath  = "Assets/Textures/UI/Title/TitleLogo_OverCleave.png";
    constexpr float       kLogoLeft  = -676.0f;         // ロゴの左端の X
    constexpr float       kLogoY     = -226.0f;         // ロゴの縦の中心
    constexpr float       kLogoW     = 640.0f;          // 表示する幅
    constexpr float       kLogoH     = kLogoW / 3.599f; // 表示する高さ（画像の縦横比 1983x551 に合わせる）
    constexpr float       kSlashTilt = 20.0f;           // 斬撃の光の傾き（度）。ロゴの赤い斬撃の向きに寄せる

    // --- ロゴの演出の時間（秒。メニューを作った時点から数える） ---
    constexpr float kLogoStart = 0.5f;  // 斬撃の光が走り始めるまで
    constexpr float kRevealDur = 0.45f; // 光が左端から右端まで走る時間
    constexpr float kFlashDur  = 0.5f;  // 走り終えた後、白い光とふくらみが収まるまで

    // --- ボタンとヒントが出始めるまでの時間（秒。タイトルの最初の Update から数える） ---
    // 見え終わるのは、これに UIAppearComponent::duration を足した頃。
    // ロゴが現れ終わる頃から少しずつ重なる間隔にして、ボタンを押せるまでを短くしている。
    constexpr float kAppearStart    = 1.3f;  // はじめる
    constexpr float kAppearSettings = 1.45f; // 設定
    constexpr float kAppearQuit     = 1.6f;  // 終了
    constexpr float kAppearHints    = 1.85f; // 操作のヒント

    // ヒントの行を横に動かす量（px）。0 だと画面の中央に残り、左に寄せたメニューから離れてしまう。
    constexpr float kHintShiftX = -350.0f;

    // メニュー全体を動かす量（px）。
    // Canvas の offset は子の UI をまとめて動かすので、文字・ボタン・ヒントの座標を1つずつ変えずに、ここだけで位置を決められる。
    // 左下へ寄せるのは、ボスと群れと手前のプレイヤーが画面の上と右にいるため。
    constexpr DirectX::XMFLOAT2 kCanvasShift{-180.0f, 220.0f};

    // --- 画面の下を暗くする半透明のパネル（px） ---
    // UI を左下に寄せているので、これが無いと操作のヒントが明るい床に重なって読めない。
    // 上端を透明にしたグラデーションにして、像の顔と胴は明るく残す。
    // 画面全体を暗くすると、絵が濁るだけで文字は読みやすくならない。
    constexpr float kScrimW       = 2560.0f; // 幅。画面より大きくして、解像度が変わっても端が見えないようにする
    constexpr float kScrimBottomH = 760.0f;  // 高さ
    constexpr float kScrimBottomY = 240.0f;  // 縦の中心。パネルの上端が画面の y ≒ 520px に来る
} // namespace

void TitleMenuSystem::Initialize()
{
    World &world = *m_world;

    // 終了の要求を置くリソース（メインスレッドの Run が毎フレーム見る）
    if (!world.HasResource<GameFlowState>())
    {
        world.AddResource<GameFlowState>();
    }

    // ボタンのクリックを購読し、actionHash で分ける
    const uint32_t kStart = CCL::Utils::HashString("start_game");
    const uint32_t kQuit  = CCL::Utils::HashString("quit");

    ListenEvent<UIButtonClickedEvent>(
        [this, kStart, kQuit](const UIButtonClickedEvent &e) {
            // 切り替えか終了を頼んだ後の連打は無視する。
            if (m_transitioning)
            {
                return;
            }

            if (e.actionHash == kStart)
            {
                // ゲームへの切り替えを頼むだけにする。フェード・裏での読み込み・World の片付けの時機は SceneManager が決める。
                m_transitioning = SceneManager::Instance().RequestTransition("Game", true);
            }
            else if (e.actionHash == kQuit)
            {
                m_transitioning = true;

                // この購読は UIButtonSystem から呼ばれ、ワーカースレッドで走ることがある。
                // ワーカーから PostQuitMessage を呼ぶと、WM_QUIT がそのスレッドのキューに入り、メインループが受け取れず終了しない。
                // そこでフラグだけを立て、メインスレッドの Run() がそれを見て終了する。
                if (m_world->HasResource<GameFlowState>())
                {
                    m_world->GetResource<GameFlowState>().quitRequested = true;
                }
            }
        });
}

void TitleMenuSystem::SpawnMenu()
{
    // 処理フロー:
    // 1. Canvas を作る
    // 2. 画面の下を暗くするパネルを作る
    // 3. タイトルロゴを作る
    // 4. ボタンを作る
    // 5. 操作のヒントを作る

    World &world = *m_world;

    // ---------------------------------------------------------
    // 1. Canvas
    // ---------------------------------------------------------

    // 以降の UI はすべてこの Canvas の子にする。
    // Canvas の四角形は常に画面全体なので、子の offset は画面を基準にした値のまま使える。
    const EntityID canvas = CCL::GameUI::SpawnCanvas(world, "TitleCanvas", kCanvasShift);

    // ---------------------------------------------------------
    // 2. 下を暗くするパネル
    // ---------------------------------------------------------

    // 描画順は一番奥にする。
    const DirectX::XMFLOAT4 scrimLowEnd{0.0f, 0.0f, 0.02f, 0.60f};
    CCL::GameUI::SpawnPanel(world, "TitleScrimBottom", {0.0f, kScrimBottomY},
                            {kScrimW, kScrimBottomH}, {0.0f, 0.0f, 0.0f, 0.0f}, 5, -1.0f, 0.0f, canvas, &scrimLowEnd);

    // ---------------------------------------------------------
    // 3. タイトルロゴ
    // ---------------------------------------------------------
    SpawnLogo(canvas);

    // ---------------------------------------------------------
    // 4. ボタン
    // ---------------------------------------------------------

    // 最初は「はじめる」を選んでおく。
    CCL::GameUI::SpawnButton(world, "BtnStart", "LblStart", "はじめる", {kMenuX, 40.0f}, "start_game",
                             true, 10, kAppearStart, canvas);
    // 「設定」は SettingsScreenSystem が "open_settings" のボタンを見て開く。
    CCL::GameUI::SpawnButton(world, "BtnSettings", "LblSettings", "設定", {kMenuX, 125.0f}, "open_settings", false,
                             10, kAppearSettings, canvas);
    CCL::GameUI::SpawnButton(world, "BtnQuit", "LblQuit", "終了", {kMenuX, 210.0f}, "quit", false,
                             10, kAppearQuit, canvas);

    // ---------------------------------------------------------
    // 5. 操作のヒント
    // ---------------------------------------------------------

    // タイトルの UI はシーンごと消えるので、作った ID は持っておかない。
    std::vector<CCL::ECS::EntityID> hints;
    CCL::GameUI::SpawnMenuHints(world, canvas, 290.0f, hints, 230, kAppearHints, kHintShiftX);
}

void TitleMenuSystem::SpawnLogo(CCL::ECS::EntityID canvas)
{
    using namespace CCL::ECS;
    World &world = *m_world;

    auto arch = ArchetypeHelper::Generate<TransformComponent, UITransformComponent, UIImageComponent>();

    // ロゴ本体と写しは左端を基準に置く。幅を縮めても左端が動かず、右へ向かって現れるように見える。
    UITransformComponent t;
    t.anchor = {0.5f, 0.5f};
    t.pivot  = {0.0f, 0.5f};
    t.offset = {kLogoLeft, kLogoY};
    t.size   = {0.0f, kLogoH}; // 最初は幅 0（AnimateLogo が広げる）

    // --- ロゴ本体 ---
    {
        auto e = EntityFactory::SpawnNamed(world, "TitleLogo", arch);

        UIImageComponent img;
        img.texturePath = kLogoPath;
        img.sortOrder   = 11;
        img.uvRect      = {0.0f, 0.0f, 0.0f, 1.0f};

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(img);
        m_logoID = e.ID();
    }

    // --- 白く光らせる写し（同じ画像を加算合成で重ね、濃さだけを動かす） ---
    {
        auto e = EntityFactory::SpawnNamed(world, "TitleLogoFlash", arch);

        UIImageComponent img;
        img.texturePath = kLogoPath;
        img.sortOrder   = 12;
        img.additive    = true;
        img.visible     = false;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(t).Set(img);
        m_logoFlashID = e.ID();
    }

    // --- 斬撃の光（細長い板を傾け、ロゴが現れる先端を走らせる） ---
    {
        auto e = EntityFactory::SpawnNamed(world, "TitleLogoSlash", arch);

        UITransformComponent st;
        st.anchor   = {0.5f, 0.5f};
        st.pivot    = {0.5f, 0.5f};
        st.offset   = {kLogoLeft, kLogoY};
        st.size     = {6.0f, kLogoH * 1.4f}; // ロゴより縦に長くして、上下にはみ出す鋭い線にする
        st.rotation = kSlashTilt;

        UIImageComponent img;
        img.color     = {1.0f, 0.55f, 0.50f, 1.0f}; // ロゴの赤い斬撃に寄せた、白っぽい赤
        img.sortOrder = 13;
        img.additive  = true;
        img.visible   = false;

        e.Set(CCL::GameUI::MakeUINode(canvas)).Set(st).Set(img);
        m_slashID = e.ID();
    }
}

void TitleMenuSystem::AnimateLogo(float dt)
{
    // 処理フロー:
    // 1. ロゴを左から現す
    // 2. 現れる先端に斬撃の光を走らせる
    // 3. 走り終えた瞬間にロゴを白く光らせる

    m_logoTime += dt;

    auto *logoT  = m_world->GetComponent<UITransformComponent>(m_logoID);
    auto *logoI  = m_world->GetComponent<UIImageComponent>(m_logoID);
    auto *flashT = m_world->GetComponent<UITransformComponent>(m_logoFlashID);
    auto *flashI = m_world->GetComponent<UIImageComponent>(m_logoFlashID);
    auto *slashT = m_world->GetComponent<UITransformComponent>(m_slashID);
    auto *slashI = m_world->GetComponent<UIImageComponent>(m_slashID);
    // 生成が反映されるまでは、部品を引けない。
    if (!logoT || !logoI || !flashT || !flashI || !slashT || !slashI)
    {
        return;
    }

    // ---------------------------------------------------------
    // 1. 現れる
    // ---------------------------------------------------------

    // 幅と切り出す範囲を一緒に広げ、画像を引き伸ばさずに左から見せていく。
    // 減速しながら進める。最初に勢いよく切り込み、右端で収まる。
    const float r      = std::clamp((m_logoTime - kLogoStart) / kRevealDur, 0.0f, 1.0f);
    const float reveal = 1.0f - (1.0f - r) * (1.0f - r);
    logoT->size.x      = kLogoW * reveal;
    logoI->uvRect.z    = reveal;

    // ---------------------------------------------------------
    // 2. 斬撃の光
    // ---------------------------------------------------------

    // 現れる先端を走り、走り終えたら 0.15 秒で細くなって消える。
    const bool running = m_logoTime >= kLogoStart && r < 1.0f;
    const float after  = std::max(0.0f, m_logoTime - (kLogoStart + kRevealDur)); // 走り終えてからの秒数
    const float fade   = std::max(0.0f, 1.0f - after / 0.15f);
    slashI->visible    = running || fade > 0.0f;
    slashT->offset.x   = kLogoLeft + kLogoW * reveal;
    slashT->size.x     = 6.0f * (running ? 1.0f : fade);
    slashI->color.w    = running ? 1.0f : fade;

    // ---------------------------------------------------------
    // 3. 光る
    // ---------------------------------------------------------

    // 走り終えた瞬間に、ロゴ全体を白く光らせ、わずかに膨らませてから戻す。
    const float f   = (r >= 1.0f) ? std::max(0.0f, 1.0f - after / kFlashDur) : 0.0f;
    const float pop = f * f; // 光の強さ。光り始めが一番強く、すぐに引く
    flashI->visible  = pop > 0.0f;
    flashI->color    = {1.0f, 1.0f, 1.0f, 0.8f * pop};
    flashT->size     = logoT->size;
    logoT->scale     = 1.0f + 0.04f * pop;
    flashT->scale    = logoT->scale;
}

void TitleMenuSystem::Update(float dt)
{
    if (!m_menuSpawned)
    {
        // 作ったエンティティは次のフレームから使える（生成は遅れて反映される）。
        SpawnMenu();
        m_menuSpawned = true;
        return;
    }

    AnimateLogo(dt);
}
