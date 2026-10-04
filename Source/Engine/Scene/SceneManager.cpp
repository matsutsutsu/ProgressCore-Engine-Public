#include "SceneManager.h"
#include "SceneFactory.h"

// 具体シーンは各 .cpp の REGISTER_SCENE で SceneFactory へ自己登録される。
// ここで include すると Engine→Game 依存になるため、名前(文字列)だけで扱う。
#include "Engine/Scene/SceneLoading.h" // 非同期ロード用のローディングシーン

#include "Engine/Serialization/Serializer/SceneSerializer.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"

#include "Engine/ECS/Core/CCL_World.h"

// フェード描画用（◆タイルワイプ）。
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Core/Math/Interpolation.h" // SmoothStep()
#include "Engine/Graphics/Renderer/Sprite.h"
#include "Engine/Audio/IAudioAPI.h" // 画面切り替えの効果音

// ロード前検証（破壊前のファイル存在確認）に使用
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"

// タグとシステム
#include "Engine/Core/Components/PersistentTag.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"
#include "Engine/Transform/Systems/HierarchyCleanupSystem.h"
#include "Engine/Physics/Systems/Jolt/JoltCleanupSystem.h"
#include "Engine/Audio/Systems/AudioCleanupSystem.h"

#include "tracy/Tracy.hpp"

#include <algorithm> // std::clamp
#include <cmath>     // std::sin / std::atan

using namespace CCL::ECS;

// m_fadeQuad（unique_ptr<Sprite>）の破棄に Sprite の完全型が要るので、ここで定義する
SceneManager::SceneManager()  = default;
SceneManager::~SceneManager() = default;

void SceneManager::Initialize()
{
    ZoneScopedN("SceneManager::Initialize");

    // シーンの登録は、各シーンの .cpp 末尾にある REGISTER_SCENE マクロが
    // 静的初期化時に済ませている（"Editor" / "Particle" / "Title" / "Game"）。
    // ここでは名前で参照するだけなので、具体的なシーンのヘッダは要らない。

    // 最初に表示するシーン。製品ビルドはタイトルから始める
    // （「起動して最初に何が出るか」も作品の一部なので、ゲームへ直行させない）
#ifdef SHIPPING_BUILD
    constexpr const char *kStartupScene = "Title";
#else
    constexpr const char *kStartupScene = "Editor";
#endif
    ChangeSceneAsync(kStartupScene);

    // 起動直後は、普段の非同期遷移で「黒を保持して裏読みしている途中」と同じ状態から始める。
    //   黒の上にスプラッシュ（ローディング画面）が出て、読み終わったら本命へ1回だけ明転する。
    //   黒からフェードインで始めると、読み終わったときに黒い画面の上でもう一度暗転し直すことになり、
    //   何も映らない黒い時間が挟まる
    m_fadeAlpha = 1.0f;
    m_fade      = FadeState::Hold;
    m_asyncHold = true;
}

void SceneManager::FixedUpdate(float fixedTime)
{
    // 切り替え待ちの間は、破棄予定のシーンの物理・ロジックを進めない
    if (nextScene == nullptr && currentScene != nullptr)
    {
        currentScene->FixedUpdate(fixedTime);
    }
}

void SceneManager::Update(float elapsedTime)
{
    ZoneScopedN("SceneManager::Update");

    // フェードを先に進める。黒に達した瞬間に「予約された切替」を発火し、
    // nextScene をセットするので、直後の切替ブロックが同じフレームで処理する。
    // （黒で完全に隠れている間に重い切替・初期化が走る＝ポップインが見えない）
    AdvanceFade(elapsedTime);

    if (nextScene != nullptr)
    {
        // シーン切替フレーム（起動直後もここ）。重い初期化が集中するので個別に計測する。
        ZoneScopedN("SceneManager: Scene Switch");

        Clear();

        // 切替の通し番号を進める。World もエンティティも作り直されるので、
        // 前のシーンを指していた参照（エディタの Undo 履歴・選択）は
        // ここを境に全部無効になる。
        ++m_sceneSerial;

        currentScene = nextScene;
        nextScene    = nullptr;

        // 1段階目: 重いデータのロード（Initialize）。
        // SceneLoading 経由で裏読み済みなら IsReady() が true なので飛ばす
        if (!currentScene->IsReady())
        {
            {
                ZoneScopedN("Scene::Initialize (heavy load)");
                currentScene->Initialize();
            }

            currentScene->SetReady();
        }

        // 2段階目: GPU へのセットアップ（Start）。
        // 必ずメインスレッドのこのタイミングで1回だけ呼ばれる。
        // dc->Map など DeviceContext を使う処理はここで行う
        {
            ZoneScopedN("Scene::Start (GPU setup)");
            currentScene->Start();
        }
    }

    if (currentScene != nullptr)
    {
        currentScene->Update(elapsedTime);
    }
}

void SceneManager::Render()
{
    if (currentScene != nullptr)
    {
        currentScene->Render();
    }

    // フェードの黒矩形は最前面。シーンの描画（UI 含む）の後に全ての上へ被せる。
    // BaseScene 側には書かず、遷移の所有者であるここで一元的に描く
    DrawFade();

    // 幕の上に描くもの（ローディングの表示）。幕が黒のままでも見えるよう、幕の後に描く
    if (currentScene != nullptr)
    {
        currentScene->RenderOverFade();
    }
}

void SceneManager::DrawGUI()
{
    if (currentScene != nullptr)
    {
        currentScene->DrawGUI();
    }
}

void SceneManager::Clear()
{
    if (currentScene != nullptr)
    {
        currentScene->Finalize();
        delete currentScene;
        currentScene = nullptr;
    }

    // SceneManager 自身は Meyer's Singleton（static ローカル変数）のため、
    // このインスタンスの実体の破棄は wWinMain リターン後の静的破棄タイミングまで
    // 起きない＝Graphics::Shutdown()（デバイス破棄・リークレポート）より後になる。
    // m_fadeQuad はここで明示的に解放しておかないと、GraphicsDevice::Shutdown()
    // の ReportLiveDeviceObjects で「真のリーク」として誤検出されてしまう。
    m_fadeQuad.reset();
}

// ============================================================
// シーンファイルの保存・読み込み
// ============================================================

void SceneManager::SaveScene(CCL::ECS::World *world, const std::string &filepath)
{
    // 未適用の破棄予約などを確定させ、破棄待ちの Entity がセーブデータに混ざらないようにする
    world->ScrutinyAndApply();

    SceneSerializer::Serialize(world, filepath);
}

void SceneManager::LoadScene(CCL::ECS::World *world, const std::string &filepath)
{
    // 0. 破棄する前に、ファイルが読めるかを確かめる。
    //   この関数は「全破棄 → デシリアライズ」の順で動くので、ファイルが欠けている/空だと
    //   破棄だけが実行され、World が丸ごと消える（実際に全 Entity が消える事故が起きた）。
    //   読めないと分かったら、今の World には一切手を付けずに中止する
    const std::string jsonStr = FileSystem::ReadAllText(filepath);
    if (jsonStr.empty())
    {
        CCL_LOG_ERROR(LogCategory::Core,
                      "[SceneManager] LoadScene 中止: ファイルが存在しないか空です (%s)。"
                      "現在のワールドは破壊しません。",
                      filepath.c_str());
        return;
    }

    // 1. 破棄する Entity に印を付ける
    auto &chunks = world->GetChunkManager().GetChunks();
    for (auto &chunkPtr : chunks)
    {
        if (!chunkPtr) continue;
        auto *chunk = chunkPtr.get();

        // PersistentTag（シーンを跨いで残す Entity）は破棄しない
        if (chunk->HasComponent<Tag::PersistentTag>()) continue;

        for (size_t i = 0; i < chunk->GetEntityCount(); ++i)
        {
            CCL::ECS::EntityID entity = chunk->GetEntityByIndex(i);
            if (entity != CCL::ECS::InvalidEntityID)
            {
                // 子孫ごと DestroyTag を付ける
                TransformUpdateSystem::MarkForDestruction(*world, entity, true);
            }
        }
    }

    // 2. 後片付けの System を直接呼び、1フレーム待たずに今すぐ物理空間とメモリから消す。
    //    DestroyTag の付与は遅延されるので、先に確定させないと下の Cleanup が対象を見つけられない
    world->ScrutinyAndApply();

    // Jolt の剛体を消す
    JoltCleanupSystem::ExecuteCleanup(*world);

    // 破棄される AudioEmitter の再生中サウンドを止める（BGM 等が遷移後も鳴り続けるのを防ぐ）
    AudioCleanupSystem::ExecuteCleanup(*world);

    // 親子リンクを外し、破棄を予約する
    HierarchyCleanupSystem::ExecuteCleanup(*world);

    // 積んだ破棄予約を今すぐ確定させる
    world->ScrutinyAndApply();

    // 3. ファイルから Entity を作り直す。
    //    ファイル内の番号で書かれた親子参照（EntityLink）は、Deserialize の中で
    //    新しい EntityID へ自動で読み替えられる
    SceneSerializer::Deserialize(world, filepath);
}

// ============================================================
// シーン切り替え
// ============================================================

void SceneManager::ChangeScene(const std::string &name)
{
    m_currentSceneName = name;

    Scene *newScene = SceneFactory::Instance().Create(name);
    if (newScene)
    {
        nextScene = newScene;
    }
}

void SceneManager::ChangeSceneAsync(const std::string &name)
{
    // 対象シーンを生成する（この時点では Initialize を走らせないので軽い）
    Scene *target = SceneFactory::Instance().Create(name);
    if (!target) return;

    // シーン名は「最終的に遷移する先」にしておく
    m_currentSceneName = name;

    // ローディング画面で包む。target の重い Initialize() は
    // SceneLoading がワーカースレッドで裏読みし、完了後に本線へ切り替える。
    ChangeScene(new SceneLoading(target));
}

void SceneManager::ChangeScene(Scene *scene)
{
    // 切り替え待ちのシーンがあれば、後から来た方で置き換える
    if (nextScene != nullptr)
    {
        delete nextScene;
    }

    nextScene = scene;
}

// ============================================================
// 画面遷移フェード
// ============================================================

bool SceneManager::RequestTransition(const std::string &name, bool async)
{
    if (IsTransitioning()) return false; // 遷移中の多重要求は無視

    // 名前指定の切り替えを黒の瞬間に実行するよう予約し、フェードアウトを始める
    m_pendingValid = true;
    m_pendingName  = name;
    m_pendingAsync = async;
    m_pendingScene = nullptr;
    m_fade         = FadeState::Out; // 現在の m_fadeAlpha（通常0）からアウト
    PlayTransitionSound();
    return true;
}

bool SceneManager::RequestTransition(Scene *readyScene)
{
    if (!readyScene) return false;

    // 非同期ロードの黒保持中に SceneLoading から呼ばれるケース。
    //   すでに画面は黒（alpha=1）で、ローディングも黒の裏で完了している。
    //   ここで「新たに暗転し直す」とチラつくので、暗転はせず本命へ差し替えて
    //   そのままフェードイン（黒→ゲーム）する。
    if (m_asyncHold)
    {
        ChangeScene(readyScene); // nextScene に本命をセット（次の Update で切り替わる）
        m_asyncHold = false;
        m_fade      = FadeState::In; // alpha は 1 のまま → ゲームへ1回だけ明転
        return true;
    }

    if (IsTransitioning()) return false;

    // 通常の実体指定の遷移（黒保持なし）は暗転から始める
    m_pendingValid = true;
    m_pendingName.clear();
    m_pendingAsync = false;
    m_pendingScene = readyScene;
    m_fade         = FadeState::Out;
    PlayTransitionSound();
    return true;
}

void SceneManager::PlayTransitionSound()
{
    // 音声は World のリソースにある。今のシーンの World から引く（無ければ鳴らさない）
    CCL::ECS::World *world = currentScene ? currentScene->GetWorld() : nullptr;
    if (!world || !world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
    if (auto &audio = world->GetResource<std::shared_ptr<IAudioAPI>>())
        audio->PlayOneShot(CCL::Utils::HashString("sfx:/ui_transition"));
}

void SceneManager::FadeInFromBlack(float dur)
{
    m_fadeInDur = (dur > 0.0001f) ? dur : 0.0001f;
    m_fadeAlpha = 1.0f; // 黒から
    m_fade      = FadeState::In;
}

void SceneManager::AdvanceFade(float dt)
{
    switch (m_fade)
    {
    case FadeState::Out:
        m_fadeAlpha += dt / m_fadeOutDur;
        if (m_fadeAlpha >= 1.0f)
        {
            m_fadeAlpha = 1.0f;
            // 完全な黒の瞬間に、予約された切り替えを実行する（nextScene がセットされる）
            BeginPendingChange();
            m_fadeHoldTimer = 0.0f;
            m_fade          = FadeState::Hold; // 黒を少し保持してから明転へ
        }
        break;

    case FadeState::Hold:
        // 非同期ロード中は黒のまま待機（明転しない）。ロード完了時に
        // SceneLoading が RequestTransition(Scene*) を呼び、そこで In へ移る。
        if (m_asyncHold) break;

        // 通常（同期切り替え）は黒を少しタメてから明転する。この裏で新シーンの
        // 初回フレームが描かれ切る
        m_fadeHoldTimer += dt;
        if (m_fadeHoldTimer >= m_fadeHoldDur)
            m_fade = FadeState::In;
        break;

    case FadeState::In:
        m_fadeAlpha -= dt / m_fadeInDur;
        if (m_fadeAlpha <= 0.0f)
        {
            m_fadeAlpha = 0.0f;
            m_fade      = FadeState::None;
        }
        break;

    case FadeState::None:
    default:
        break;
    }
}

void SceneManager::BeginPendingChange()
{
    if (!m_pendingValid) return;
    m_pendingValid = false;

    if (m_pendingScene)
    {
        // 実体指定: SceneLoading が裏読みを終えた本命シーンへ切り替える
        ChangeScene(m_pendingScene);
        m_pendingScene = nullptr;
    }
    else if (m_pendingAsync)
    {
        // 名前 + 非同期: 黒のまま裏読みする（ローディング画面は見せない）。
        // 完了まで黒を保持し、本命が用意できたら「1回だけ」フェードインする
        ChangeSceneAsync(m_pendingName);
        m_asyncHold = true;
    }
    else
    {
        // 名前 + 同期: その場で切り替える（重い Initialize は黒の裏で走る）
        ChangeScene(m_pendingName);
    }
}

void SceneManager::DrawFade()
{
    if (m_fadeAlpha <= 0.001f) return; // 透明なら描かない

    auto &gfx = Graphics::Instance();
    auto *dc  = gfx.GetDeviceContext();
    if (!dc) return;

    // 半透明合成・深度無効を明示的に設定する（他パスの状態に依存しない）。
    //   合成は RT の α を書き換えない UITransparency。縁の半透明の◆が α の穴を開けると、
    //   エディタの GameView でその部分だけ後ろのウィンドウが透ける
    dc->OMSetBlendState(
        gfx.GetRenderState()->GetBlendState(BlendState::UITransparency), nullptr, 0xffffffff);
    dc->OMSetDepthStencilState(
        gfx.GetRenderState()->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

    if (!m_fadeQuad) m_fadeQuad = std::make_unique<Sprite>(gfx.GetDevice()); // 白のダミーテクスチャ。色を掛けて単色矩形にする

    const float sw = gfx.GetScreenWidth();
    const float sh = gfx.GetScreenHeight();

    // 完全な黒の間（暗転しきった・ローディング中）は、1枚の黒で確実に覆う
    if (m_fadeAlpha >= 0.999f)
    {
        m_fadeQuad->Render(dc, 0.0f, 0.0f, 0.0f, sw, sh, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // --- ◆タイルワイプ ---
    //   画面を◆の格子に区切り、左から右へ斜めに、◆が育って画面を埋める（暗転）。
    //   明転は同じ向きのまま◆が縮んで消える（左から先に晴れ、動きが右へ続く）。
    //   育つ途中の◆は白金の縁で光り、波の先頭には細い光の筋が走る
    constexpr float kCellPx = 96.0f;  // 1080p での格子の間隔
    constexpr float kBand   = 0.35f;  // 1つの◆が育ちきるまでに波が進む幅（画面幅に対する割合）
    constexpr float kSlope  = 0.35f;  // 波の傾き。下ほど遅れて届く（画面の高さ 1 に対する横のずれ）
    constexpr float kGoldR = 0.96f, kGoldG = 0.88f, kGoldB = 0.62f; // HUD の白金

    const bool  revealing = (m_fade == FadeState::In);
    const float p         = CCL::Math::SmoothStep(revealing ? 1.0f - m_fadeAlpha : m_fadeAlpha); // 波の進み具合 0〜1
    const float front     = p * (1.0f + kBand);                                                  // 波の先頭の位置（u 座標）
    const float k         = sh / 1080.0f;
    const float cell      = kCellPx * k;
    const float span      = sw + sh * kSlope; // u 座標 0〜1 に対応する画面上の長さ

    // 各セルの育ち具合 0〜1（暗転では 1 が黒で覆った状態。明転では反転して、先に波が来た所から晴れる）
    auto coverage = [&](float x, float y) {
        const float u = (x + y * kSlope) / span;
        const float t = std::clamp((front - u) / kBand, 0.0f, 1.0f);
        return revealing ? 1.0f - t : t;
    };
    // 中心 (cx, cy)・対角線の半分 half の◆を描く（矩形を 45 度回す。辺の長さは half * √2）
    auto diamond = [&](float cx, float cy, float half, float r, float g, float b, float a) {
        const float side = half * 1.41421356f;
        m_fadeQuad->Render(dc, cx - side * 0.5f, cy - side * 0.5f, 0.0f, side, side, 45.0f, r, g, b, a);
    };

    // セルの◆が四隅まで覆うには、対角線の半分がセルの幅と同じだけ要る（少し足して隙間を消す）
    const float fullHalf = cell + 1.5f;
    const int   cols     = static_cast<int>(sw / cell) + 2;
    const int   rows     = static_cast<int>(sh / cell) + 2;

    // 1 周目: 育つ途中の◆の白金の縁。2 周目の黒が上から覆うので、隣が黒く埋まった所には残らない
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const float cx = (static_cast<float>(col) + 0.5f) * cell;
            const float cy = (static_cast<float>(row) + 0.5f) * cell;
            const float c  = coverage(cx, cy);
            if (c <= 0.0f || c >= 1.0f) continue;
            const float rim = std::sin(c * 3.1415927f); // 育ち始めと育ちきりで消え、途中で最も明るい
            diamond(cx, cy, fullHalf * c, kGoldR, kGoldG, kGoldB, 0.85f * rim);
        }
    }

    // 2 周目: 黒の◆。縁より一回り小さく始め、育ちきると縁を飲み込んでセルを覆う
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const float cx = (static_cast<float>(col) + 0.5f) * cell;
            const float cy = (static_cast<float>(row) + 0.5f) * cell;
            const float c  = coverage(cx, cy);
            if (c <= 0.0f) continue;
            const float inner = 0.8f + 0.2f * c; // 縁の幅の分だけ小さく描く（育ちきると縁と同じ大きさ）
            diamond(cx, cy, fullHalf * c * inner, 0.0f, 0.0f, 0.0f, 1.0f);
        }
    }

    // 波の先頭を走る細い光の筋（斬撃の切れ味）。波の出入りの両端では消す
    const float streakA = std::sin(p * 3.1415927f);
    if (streakA > 0.01f)
    {
        // u = front の線は x + y * kSlope = front * span。画面の縦の中央を通る点から、傾けて縦に伸ばす
        const float x     = front * span - sh * 0.5f * kSlope;
        const float angle = std::atan(kSlope) * 57.29578f; // 下ほど左へ寄る向き
        const float len   = sh * 1.3f;
        m_fadeQuad->Render(dc, x - 12.0f * k, sh * 0.5f - len * 0.5f, 0.0f, 24.0f * k, len, angle,
                           kGoldR, kGoldG, kGoldB, 0.18f * streakA); // 淡い光のにじみ
        m_fadeQuad->Render(dc, x - 1.5f * k, sh * 0.5f - len * 0.5f, 0.0f, 3.0f * k, len, angle,
                           1.0f, 0.97f, 0.88f, 0.9f * streakA); // 芯
    }
}
