#include "SceneLoading.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/RenderState.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Text/TextRenderer.h"
#include "Engine/Graphics/Renderer/Sprite.h"
#include "Engine/Core/Math/Interpolation.h"

#include <objbase.h>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr float kProgressDamping = 8.0f; // 表示の進捗が目標値へ追いつく速さ（Damp の speed）

    // 表示の進捗がここに届いたら完了とみなし、完了の演出（READY・リングの深紅）を始める
    //   進捗は指数的に 100% へ近づき、ちょうど 1.0 には届かないため、少し手前で区切る
    //   ログの最後の行もこの値で出し、完了の演出と同じ瞬間にそろえる
    constexpr float kDoneProgress = 0.98f;

    constexpr float kDotR = 0.96f, kDotG = 0.88f, kDotB = 0.62f; // 飾り（線・◆・目盛り）の白金。HUD の色にそろえる

    // ログの1行
    struct LogLine
    {
        float       at;   // 見せかけの進捗がこの値に届いたら出す（0〜1）
        const char *text; // 時刻の後ろに出す本文
    };

    // ログの台本。読み込みの段階に見立てた行を並べ、待っている間に作品の中身（AI・陣形・群れ）を伝える
    // 実際の読み込みの段階とは連動しない（裏読みの途中経過は外から取れないため）
    constexpr LogLine kLogLines[kLogLineCount] = {
        {0.00f, "SYS   INIT WORLD"},
        {0.12f, "SYS   MEMORY CHECK ... OK"},
        {0.24f, "GFX   SHADER CACHE ... OK"},
        {0.36f, "GFX   LOADING STAGE"},
        {0.48f, "UNIT  LOADING PLAYER"},
        {0.60f, "UNIT  LOADING ENEMY : VALKYRA"},
        {0.72f, "AI    LOADING DRONES"},
        {kDoneProgress, "SYS   ALL READY"},
    };

    // 読み込みのたびに1つ出すヒント。実際の仕様に沿った内容だけを書く（HP50% で第2形態: BossPhaseComponent）
    constexpr const char *kTips[] = {
        "震えたドローンは、まもなく突撃してくる。",
        "ドローンの突撃をぎりぎりでかわすと、ジャスト回避が始まる。",
        "ジャスト回避の後は、そのまま反撃のラッシュへつながる。",
        "攻撃が来たらダッシュで緊急回避する。",
        "敵の攻撃の後隙を見極め、一撃を叩き込め。",
    };
    constexpr int kTipCount = static_cast<int>(sizeof(kTips) / sizeof(kTips[0]));

    int  g_tipIndex  = 0;    // 次の読み込みで出すヒントの番号
    bool g_firstLoad = true; // アプリを起動してから、まだ一度も読み込んでいないか

    // x から 0 以上 1 未満の決まった乱数を返す。粒子のノイズの散らばりと、出始めのちらつきに使う
    float Hash(float x)
    {
        const float v = std::sin(x * 12.9898f) * 43758.5453f;
        return v - std::floor(v);
    }
}

// ---------------------------------------------------------------------------
// 生成・破棄
// ---------------------------------------------------------------------------

SceneLoading::SceneLoading(Scene *nextScene) : m_nextScene(nextScene) {}



SceneLoading::~SceneLoading()
{
    // ロードジョブが走っていれば、終わるまで待ってから破棄する
    // 通常は Finalize で待ち終えているのですぐ返る。Finalize を経ずに破棄されたときの安全策
    SceneManager::Instance().GetLoaderJobs().WaitForCounter(&m_loadCounter);
}



// ---------------------------------------------------------------------------
// 初期化・終了・更新
// ---------------------------------------------------------------------------

void SceneLoading::Initialize()
{
    // 処理フロー:
    // 1. 共通の初期化
    // 2. この読み込みで出す表示を決める
    // 3. 次のシーンの裏読みを始める

    // ---------------------------------------------------------
    // 1. 共通の初期化
    // ---------------------------------------------------------

    // カメラ・リソースの登録だけを行う
    // SceneLoading は System を登録しないので軽い
    BaseScene::Initialize();

    m_isFinished = false;

    // ---------------------------------------------------------
    // 2. 表示の種類
    // ---------------------------------------------------------
    m_tipIndex = g_tipIndex++ % kTipCount;

    // ロゴは製品ビルドの、起動して最初の読み込みでだけ出す
    // 開発ビルドでは出さず、起動時は読み込みが終わり次第抜けて、すぐ触れるようにする
#ifdef SHIPPING_BUILD
    m_splash = g_firstLoad;
#else
    m_splash = false;
    m_quick  = g_firstLoad;
#endif
    g_firstLoad = false;
    m_logStart.fill(-1.0f);

    // ---------------------------------------------------------
    // 3. 裏読み
    // ---------------------------------------------------------
    if (m_nextScene)
    {
        Scene             *target   = m_nextScene;
        std::atomic<bool> *finished = &m_isFinished;

        // ジョブはワーカースレッドで実行される
        // WaitForCounter で待たずに投げっぱなしにし、完了は Update で m_isFinished を見て知る
        // m_loadCounter は、Finalize とデストラクタで完了を待つために渡す
        SceneManager::Instance().GetLoaderJobs().Execute(
            [target, finished]() {
                // WIC 経由のテクスチャ読み込みには、このスレッドでの COM 初期化が要る
                HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

                // 次のシーンの重い読み込み
                // DeviceContext（描画コマンド）は触らないこと。メインスレッドの描画と競合する
                // リソースの生成（CreateBuffer / CreateTexture）はスレッドセーフなので行ってよい
                target->Initialize();
                target->SetReady();

                if (SUCCEEDED(hr))
                {
                    CoUninitialize();
                }

                // 完了は最後に release で書く
                // メインスレッドが acquire で true を読んだ時点で、Initialize が作った World の中身もすべて見えている
                finished->store(true, std::memory_order_release);
            },
            &m_loadCounter);
    }
}



void SceneLoading::Finalize()
{
    // ロードジョブが終わるまで待つ（スレッドの join にあたる）
    // 裏読みの途中で this を壊すと、ジョブが m_isFinished へ書き込めなくなる
    SceneManager::Instance().GetLoaderJobs().WaitForCounter(&m_loadCounter);

    BaseScene::Finalize();
}



void SceneLoading::Update(float elapsedTime)
{
    // 処理フロー:
    // 1. 時間を進め、裏読みが終わったかを読む
    // 2. 見せかけの進捗を進める
    // 3. ログの各行が出る時刻を記録する
    // 4. 表示の濃さを決める
    // 5. 条件がそろったら次のシーンへ切り替える

    // ---------------------------------------------------------
    // 1. 時間と完了
    // ---------------------------------------------------------
    time += elapsedTime;

    BaseScene::Update(elapsedTime);

    // ワーカースレッドの release と対になる acquire で読む
    const bool done = m_isFinished.load(std::memory_order_acquire);

    // 最低表示時間の間は、裏読みが終わっていても表示を続ける
    // ローディング画面はログが最後まで流れる長さ、スプラッシュはロゴを読める長さにしている
    const float minShow = m_splash ? kSplashTime : m_quick ? 0.0f : kMinShowTime;
    const bool  holding = time < minShow;

    // ---------------------------------------------------------
    // 2. 見せかけの進捗
    // ---------------------------------------------------------

    // 裏読みの途中経過は取れない（Initialize の中は外から見えない）ので、進捗は時間で作る

    // 裏読み中は 90% へ近づき続ける
    // 長くかかっても止まって見えず、終わる前に 100% へ届くこともない

    // 裏読みが終わったら 100% へ伸ばす
    // 最低表示時間の間は、その時間いっぱいを使って 100% にする
    float target = done ? 1.0f : 0.9f * (1.0f - std::exp(-time / kProgressTau));
    if (done && holding)
    {
        target = std::min(1.0f, time / minShow);
    }
    m_progress += (target - m_progress) * CCL::Math::Damp(kProgressDamping, elapsedTime);

    // 指数的に近づくので、kDoneProgress に届いたら 100% とみなして完了の演出を始める
    if (m_progress >= kDoneProgress)
    {
        m_outroTime += elapsedTime;
    }

    // ---------------------------------------------------------
    // 3. ログ
    // ---------------------------------------------------------

    // 進捗が各行の値に届いた時刻を記録する。行はこの時刻から1文字ずつ打ち込む
    // 見出しが組み上がる前（表示し始めから 0.4 秒）には出さない
    for (int i = 0; i < kLogLineCount; ++i)
    {
        if (m_logStart[i] < 0.0f && m_progress >= kLogLines[i].at && time >= kShowDelay + 0.4f)
        {
            m_logStart[i] = time;
        }
    }

    // ---------------------------------------------------------
    // 4. 表示の濃さ
    // ---------------------------------------------------------

    // 裏読み中は kShowDelay だけ待ってから出す
    // すぐ終わる読み込みで一瞬だけ表示が出ると、かえってちらついて見える

    // 裏読みが終わった後は、出していれば完了の演出（kOutroHold 秒）を見せてから消す
    // 出していなければ出さないまま次へ進む
    float visTarget = 0.0f;
    if (!done || holding)
    {
        visTarget = (time >= kShowDelay) ? 1.0f : 0.0f;
    }
    else
    {
        visTarget = (m_visibility > 0.0f && m_outroTime < kOutroHold) ? 1.0f : 0.0f;
    }

    const float step = kFadeSpeed * elapsedTime;
    m_visibility     = (visTarget > m_visibility) ? std::min(visTarget, m_visibility + step)
                                                  : std::max(visTarget, m_visibility - step);

    // ---------------------------------------------------------
    // 5. 次のシーンへ
    // ---------------------------------------------------------

    // RequestTransition は、黒の幕の保持を解いて次のシーンへ差し替え、フェードインを始める

    // holding も条件に入れる
    // 入れないと、表示が出始める前（kShowDelay より前）に裏読みが終わったとき、
    // 表示の濃さがまだ 0 なので「消えた」とみなされ、ローディング画面を一度も出さずに抜けてしまう
    // 製品ビルドは読み込みが速く、毎回この条件に当たる

    // kMinLoadTime は、読み込みが一瞬で終わったときにも黒を一拍置くための最低の待ち時間
    if (done && !holding && time >= kMinLoadTime && m_visibility <= 0.0f && m_nextScene)
    {
        if (SceneManager::Instance().RequestTransition(m_nextScene))
        {
            m_nextScene = nullptr;
        }
    }
}



// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void SceneLoading::RenderOverFade()
{
    // 処理フロー:
    // 1. 描画の道具を用意する
    // 2. 合成と深度の設定を入れる
    // 3. スプラッシュか、ローディング画面を描く

    if (m_visibility <= 0.0f)
    {
        return;
    }

    // TextRenderer と Sprite は自前で完結し、ResourceManager のモデルに触れない
    // そのため、裏で読み込み中でもメインスレッドだけで安全に描ける
    auto         &gfx    = Graphics::Instance();
    auto         *dc     = gfx.GetDeviceContext();
    ID3D11Device *device = gfx.GetDevice();

    // ---------------------------------------------------------
    // 1. 描画の道具
    // ---------------------------------------------------------

    // 半角英数字は等幅の Ricty、日本語（ヒントなど）は M PLUS 1 で描く
    if (!m_text)
    {
        m_text = std::make_unique<TextRenderer>();
        m_text->Initialize(device, "Assets/Fonts/RictyDiminished-Regular.ttf", 48.0f, "Assets/Fonts/MPLUS1-Medium.ttf");
    }
    if (!m_white)
    {
        m_white = std::make_unique<Sprite>(device);
    }

    // ---------------------------------------------------------
    // 2. 合成と深度
    // ---------------------------------------------------------

    // 幕の上に描くので、合成と深度は自分で設定する
    // 幕が透明で描かれなかったフレームでも、前の描画の設定を引き継がないようにする

    // 合成は RT の α を書き換えない UITransparency にする
    // Transparency だと半透明の四角形が α に穴を開け、エディタの GameView でその部分だけ後ろのウィンドウが透ける
    dc->OMSetBlendState(gfx.GetRenderState()->GetBlendState(BlendState::UITransparency), nullptr, 0xffffffff);
    dc->OMSetDepthStencilState(gfx.GetRenderState()->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

    // ---------------------------------------------------------
    // 3. 描く
    // ---------------------------------------------------------

    // 濃さが線形のままだと出入りが機械的に見えるので、両端をゆるやかにする
    const float alpha = CCL::Math::SmoothStep(m_visibility);
    if (m_splash)
    {
        DrawSplash(alpha);
        return;
    }
    DrawBackdrop(alpha);
    DrawScreen(alpha);
}



void SceneLoading::DrawQuad(float cx, float cy, float w, float h, float angle, float r, float g, float b, float a)
{
    // Sprite は左上の位置で置き、四角形の中心で回す。中心の位置から左上へ戻して渡す
    m_white->Render(Graphics::Instance().GetDeviceContext(), cx - w * 0.5f, cy - h * 0.5f, 0.0f, w, h, angle,
                    r, g, b, a);
}



void SceneLoading::DrawSplash(float alpha)
{
    // 処理フロー:
    // 1. 地を塗る
    // 2. ◆と線を描く
    // 3. 文字を描く
    // 4. 読み込みが長引いたら、待っている印を出す

    auto       &gfx = Graphics::Instance();
    auto       *dc  = gfx.GetDeviceContext();
    const float sw  = gfx.GetScreenWidth();
    const float sh  = gfx.GetScreenHeight();
    const float k   = sh / 1080.0f; // 1080p を基準にした拡大率
    const float cx  = sw * 0.5f;
    const float cy  = sh * 0.5f;

    // 出てからの秒数。要素ごとに開始を少しずつ遅らせ、0.4 秒かけて出す
    const float t      = std::max(0.0f, time - kShowDelay);
    auto        fadeIn = [&](float start) { return std::clamp((t - start) / 0.4f, 0.0f, 1.0f) * alpha; };

    // ---------------------------------------------------------
    // 1. 地
    // ---------------------------------------------------------

    // ほぼ黒の1色にして、ロゴだけを見せる
    DrawQuad(cx, cy, sw, sh, 0.0f, 0.02f, 0.02f, 0.03f, alpha);

    // ---------------------------------------------------------
    // 2. ◆と線
    // ---------------------------------------------------------
    DrawQuad(cx, cy - 92.0f * k, 10.0f * k, 10.0f * k, 45.0f, kDotR, kDotG, kDotB, fadeIn(0.0f));

    // 線は中央から左右へ、減速しながら伸びる
    {
        const float d    = std::clamp((t - 0.3f) / 0.5f, 0.0f, 1.0f);
        const float draw = 1.0f - (1.0f - d) * (1.0f - d);
        const float w    = 560.0f * k * draw;
        if (w > 0.0f)
        {
            DrawQuad(cx, cy + 58.0f * k, w, 2.0f * k, 0.0f, kDotR, kDotG, kDotB, 0.8f * alpha);
        }
    }

    // ---------------------------------------------------------
    // 3. 文字
    // ---------------------------------------------------------
    if (!m_text || !m_text->IsReady())
    {
        return;
    }

    auto centered = [&](const char *s, float y, float px, float r, float g, float b, float a) {
        const float w = m_text->MeasureWidth(s, px);
        m_text->DrawText(dc, s, cx - w * 0.5f, y, px, DirectX::XMFLOAT4{r, g, b, a});
    };

    // 等幅のフォントなので、空白を挟んで字間を広げ、ロゴらしく見せる
    centered("P O W E R E D   B Y", cy - 64.0f * k, 18.0f * k, 0.93f, 0.91f, 0.86f, 0.6f * fadeIn(0.1f));
    centered("PROGRESSCORE ENGINE", cy - 30.0f * k, 60.0f * k, 0.93f, 0.91f, 0.86f, fadeIn(0.15f));
    centered("C++20  /  DIRECTX 11  /  ECS", cy + 76.0f * k, 16.0f * k, 0.93f, 0.91f, 0.86f, 0.5f * fadeIn(0.6f));

    // ---------------------------------------------------------
    // 4. 待っている印
    // ---------------------------------------------------------

    // 表示時間を過ぎても読み込みが終わっていなければ、右下に3つの点を順に点ける
    // 何も動かないと、止まっているように見える
    if (time >= kSplashTime && !m_isFinished.load(std::memory_order_acquire))
    {
        const int step = static_cast<int>(time * 3.0f) % 4; // 点いている数（0〜3）
        for (int i = 0; i < 3; ++i)
        {
            DrawQuad(sw - 120.0f * k + 18.0f * k * static_cast<float>(i), sh - 80.0f * k, 8.0f * k, 8.0f * k, 0.0f, kDotR,
                     kDotG, kDotB, (i < step ? 1.0f : 0.2f) * alpha);
        }
    }
}



void SceneLoading::DrawBackdrop(float alpha)
{
    // 処理フロー:
    // 1. 地を塗る
    // 2. 周辺を暗くする
    // 3. 粒子ノイズを散らす

    auto       &gfx = Graphics::Instance();
    auto       *dc  = gfx.GetDeviceContext();
    const float sw  = gfx.GetScreenWidth();
    const float sh  = gfx.GetScreenHeight();
    const float k   = sh / 1080.0f; // 1080p を基準にした拡大率

    // 四隅の色を変えた1枚の四角形を描く。頂点の間の色は GPU がなめらかに補間する
    // 色の順は 左上・右上・左下・右下
    auto gradient = [&](float x, float y, float w, float h, DirectX::XMFLOAT4 tl, DirectX::XMFLOAT4 tr,
                        DirectX::XMFLOAT4 bl, DirectX::XMFLOAT4 br) {
        const DirectX::XMFLOAT4 corners[4] = {tl, tr, bl, br};
        m_white->Render(dc, x, y, 0.0f, w, h, 0.0f, 0.0f, m_white->GetWidth(), m_white->GetHeight(), 0.0f, corners);
    };

    // ---------------------------------------------------------
    // 1. 地
    // ---------------------------------------------------------

    // 真っ黒にせず、青みのある墨色にする
    // 文字のある左上を明るくし、右下へ沈める
    gradient(0.0f, 0.0f, sw, sh,
             {0.150f, 0.155f, 0.175f, alpha}, {0.095f, 0.100f, 0.120f, alpha},
             {0.075f, 0.080f, 0.095f, alpha}, {0.035f, 0.038f, 0.050f, alpha});

    // ---------------------------------------------------------
    // 2. 周辺減光
    // ---------------------------------------------------------

    // 四辺から内側へ、頂点の色の補間でなめらかに暗くする
    // 暗さを段にすると、内側に四角い枠が見える
    {
        const float             ex = sw * 0.30f;
        const float             ey = sh * 0.30f;
        const DirectX::XMFLOAT4 dark{0.0f, 0.0f, 0.0f, 0.55f * alpha};
        const DirectX::XMFLOAT4 none{0.0f, 0.0f, 0.0f, 0.0f};
        gradient(0.0f, 0.0f, ex, sh, dark, none, dark, none);    // 左
        gradient(sw - ex, 0.0f, ex, sh, none, dark, none, dark); // 右
        gradient(0.0f, 0.0f, sw, ey, dark, dark, none, none);    // 上
        gradient(0.0f, sh - ey, sw, ey, none, none, dark, dark); // 下
    }

    // ---------------------------------------------------------
    // 3. 粒子ノイズ
    // ---------------------------------------------------------

    // 1秒に 24 回散らし直す細かな点で、のっぺりした面にフィルムのような質感を足す
    {
        constexpr int kGrains = 1600;

        // 種は 64 通りで使い回す。時間をそのまま種にすると、値が大きくなって sin の精度が落ちる
        const float seed = std::fmod(std::floor(time * 24.0f), 64.0f) * 17.0f;
        for (int i = 0; i < kGrains; ++i)
        {
            const float fi = static_cast<float>(i);
            const float x  = Hash(seed + fi * 1.37f) * sw;
            const float y  = Hash(seed + fi * 2.71f + 0.5f) * sh;
            const float s  = (Hash(fi * 0.91f + seed) < 0.3f ? 2.0f : 1.0f) * k;
            DrawQuad(x, y, s, s, 0.0f, 1.0f, 1.0f, 1.0f, 0.10f * alpha);
        }
    }
}



void SceneLoading::DrawScreen(float alpha)
{
    // 処理フロー:
    // 1. 時計と色を用意する
    // 2. 方眼を描く
    // 3. 見出しの下の線を描く
    // 4. 右下の進捗リングを描く
    // 5. ヒントの上の区切り線を描く
    // 6. 見出しを描く
    // 7. ログを描く
    // 8. ヒントを描く

    auto       &gfx = Graphics::Instance();
    auto       *dc  = gfx.GetDeviceContext();
    const float sw  = gfx.GetScreenWidth();
    const float sh  = gfx.GetScreenHeight();
    const float k   = sh / 1080.0f; // 1080p を基準にした拡大率

    // ---------------------------------------------------------
    // 1. 時計と色
    // ---------------------------------------------------------

    // 文字は少し温かみのある白、完了の合図は深紅。飾りの線は白金（kDot）
    constexpr float kTextR = 0.93f, kTextG = 0.91f, kTextB = 0.86f;
    constexpr float kRedR = 0.95f, kRedG = 0.18f, kRedB = 0.22f;

    const float boot  = std::max(0.0f, time - kShowDelay);                // 表示が出始めてからの秒数
    const float lock  = std::clamp(m_outroTime / 0.25f, 0.0f, 1.0f);      // 完了の合図の進み具合（0〜1）
    const float lockE = 1.0f - (1.0f - lock) * (1.0f - lock);             // lock を減速しながら止まるよう整形したもの

    // 表示が出始めた直後の 0.2 秒だけ、全体をちらつかせる
    if (boot < 0.2f && Hash(std::floor(time * 30.0f) + 0.5f) < 0.35f)
    {
        alpha *= 0.3f;
    }

    const float left   = 96.0f * k;               // 左の余白。見出し・ログ・ヒントの左端をここにそろえる
    const float headPx = 96.0f * k;               // 見出しの文字の高さ
    const float headY  = 72.0f * k;               // 見出しの上端
    const float ruleY  = headY + headPx + 18.0f * k; // 見出しの下の線の高さ
    const float ruleW  = 760.0f * k;              // 見出しの下の線の長さ
    const float tipY   = sh - 160.0f * k;         // ヒントの行の上端

    // ---------------------------------------------------------
    // 2. 方眼
    // ---------------------------------------------------------

    // 線は控えめにし、交点に小さな十字を置いて方眼だと読めるようにする
    const float grid  = 120.0f * k;
    const float cross = 9.0f * k; // 交点の十字の腕の長さ（端から端）
    for (float x = grid; x < sw; x += grid)
    {
        DrawQuad(x, sh * 0.5f, 1.0f, sh, 0.0f, kDotR, kDotG, kDotB, 0.2f * alpha);
    }
    for (float y = grid; y < sh; y += grid)
    {
        DrawQuad(sw * 0.5f, y, sw, 1.0f, 0.0f, kDotR, kDotG, kDotB, 0.2f * alpha);
    }
    for (float x = grid; x < sw; x += grid)
    {
        for (float y = grid; y < sh; y += grid)
        {
            DrawQuad(x, y, cross, 1.0f, 0.0f, kDotR, kDotG, kDotB, 0.20f * alpha);
            DrawQuad(x, y, 1.0f, cross, 0.0f, kDotR, kDotG, kDotB, 0.20f * alpha);
        }
    }

    // ---------------------------------------------------------
    // 3. 見出しの下の線
    // ---------------------------------------------------------

    // 左から減速しながら引かれ、先端に◆を置く
    {
        const float t    = std::clamp((boot - 0.15f) / 0.35f, 0.0f, 1.0f);
        const float draw = 1.0f - (1.0f - t) * (1.0f - t);
        if (draw > 0.0f)
        {
            DrawQuad(left + ruleW * draw * 0.5f, ruleY, ruleW * draw, 2.0f * k, 0.0f, kDotR, kDotG, kDotB, 0.7f * alpha);
            DrawQuad(left + ruleW * draw, ruleY, 7.0f * k, 7.0f * k, 45.0f, kDotR, kDotG, kDotB, alpha);
        }
    }

    // ---------------------------------------------------------
    // 4. 進捗リング
    // ---------------------------------------------------------

    // 真上から時計回りに、進捗の分だけ目盛りを点ける
    // 完了の合図が進むと、白金から深紅へ変わる
    {
        const float cx    = sw - 170.0f * k;
        const float cy    = sh - 175.0f * k;
        const float R     = 66.0f * k;
        const float ringR = kDotR + (kRedR - kDotR) * lockE;
        const float ringG = kDotG + (kRedG - kDotG) * lockE;
        const float ringB = kDotB + (kRedB - kDotB) * lockE;

        constexpr int kTicks = 48;
        for (int i = 0; i < kTicks; ++i)
        {
            const float u   = static_cast<float>(i) / kTicks;
            const float a   = 6.2831853f * u - 1.5707963f; // 真上（-90 度）から始める
            const bool  lit = u < m_progress;

            // 縦長の四角形を角度 + 90 度回し、長い辺を半径の向きにそろえる
            DrawQuad(cx + std::cos(a) * R, cy + std::sin(a) * R, 3.0f * k, 10.0f * k, a * 57.29578f + 90.0f, ringR, ringG,
                     ringB, (lit ? 0.95f : 0.18f) * alpha);
        }

        if (m_text && m_text->IsReady())
        {
            // --- リングの内側のパーセント ---
            const float px = 22.0f * k;
            char        buf[16];
            std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(m_progress * 100.0f + 0.5f));
            const float w = m_text->MeasureWidth(buf, px);
            m_text->DrawText(dc, buf, cx - w * 0.5f, cy - px * 0.55f, px, DirectX::XMFLOAT4{ringR, ringG, ringB, alpha});

            // --- リングの下の文字 ---

            // 読み込み中は NOW LOADING、完了したら READY
            // READY は最初の 0.3 秒だけ点滅させ、完了したことに気付けるようにする
            const float sub = 19.0f * k;
            if (lock <= 0.0f)
            {
                const float lw = m_text->MeasureWidth("NOW LOADING", sub);
                m_text->DrawText(dc, "NOW LOADING", cx - lw * 0.5f, cy + R + 24.0f * k, sub,
                                 DirectX::XMFLOAT4{kTextR, kTextG, kTextB, 0.5f * alpha});
            }
            else if (m_outroTime > 0.3f || std::fmod(m_outroTime, 0.15f) < 0.09f)
            {
                const float lw = m_text->MeasureWidth("READY", sub);
                m_text->DrawText(dc, "READY", cx - lw * 0.5f, cy + R + 24.0f * k, sub,
                                 DirectX::XMFLOAT4{kRedR, kRedG, kRedB, alpha});
            }
        }
    }

    // ---------------------------------------------------------
    // 5. ヒントの上の区切り線
    // ---------------------------------------------------------
    {
        const float t = std::clamp((boot - 0.4f) / 0.35f, 0.0f, 1.0f);
        if (t > 0.0f)
        {
            DrawQuad(left + ruleW * t * 0.5f, tipY - 30.0f * k, ruleW * t, 1.0f, 0.0f, kDotR, kDotG, kDotB, 0.3f * alpha);
        }
    }

    // ここから先は文字だけなので、フォントが用意できていなければ描かない
    if (!m_text || !m_text->IsReady())
    {
        return;
    }

    // 文字列の先頭から n 文字だけを返す。時間で n を増やすと、1文字ずつ打ち込む表示になる
    // 数えるのはバイトではなく文字なので、日本語を文字の途中で切らない
    auto typed = [](const std::string &s, float n) {
        return TextRenderer::Utf8Prefix(s, static_cast<size_t>(std::max(n, 0.0f)));
    };
    const DirectX::XMFLOAT4 textCol{kTextR, kTextG, kTextB, alpha};

    // ---------------------------------------------------------
    // 6. 見出し
    // ---------------------------------------------------------

    // 大きな LOADING を左から減速しながら滑り込ませ、右に3つの四角を順に点ける
    {
        const float t     = std::clamp(boot / 0.35f, 0.0f, 1.0f);
        const float slide = (1.0f - t) * (1.0f - t) * 40.0f * k; // 定位置までの残りの距離（px）
        m_text->DrawText(dc, "LOADING", left - slide, headY, headPx, DirectX::XMFLOAT4{kTextR, kTextG, kTextB, t * alpha});

        const float dotsX = left - slide + m_text->MeasureWidth("LOADING", headPx) + 28.0f * k;
        const int   step  = static_cast<int>(time * 3.0f) % 4; // 点いている数（0〜3）
        for (int i = 0; i < 3; ++i)
        {
            DrawQuad(dotsX + 20.0f * k * i, headY + headPx * 0.72f, 10.0f * k, 10.0f * k, 0.0f,
                     kDotR, kDotG, kDotB, (i < step ? 1.0f : 0.2f) * t * alpha);
        }
    }

    // ---------------------------------------------------------
    // 7. ログ
    // ---------------------------------------------------------

    // 出た時刻・本文の順に、1行ずつ打ち込む
    // 最新の行だけ白金で強調し、古い行は沈める
    {
        const float px     = 20.0f * k;
        const float lineH  = 32.0f * k;
        const float top    = ruleY + 70.0f * k;
        const float bodyX  = left + m_text->MeasureWidth("00.00  ", px);
        int         newest = -1;
        for (int i = 0; i < kLogLineCount; ++i)
        {
            if (m_logStart[i] >= 0.0f)
            {
                newest = i;
            }
        }

        for (int i = 0; i <= newest; ++i)
        {
            if (m_logStart[i] < 0.0f)
            {
                continue;
            }

            const float y = top + lineH * static_cast<float>(i);
            const float n = (time - m_logStart[i]) * 60.0f; // 表示する文字数（1秒に 60 文字）

            // 時刻は、表示が出始めた時点を 0 秒として数える
            char stamp[16];
            std::snprintf(stamp, sizeof(stamp), "%05.2f", std::max(0.0f, m_logStart[i] - kShowDelay));

            const bool        isNewest = (i == newest);
            const std::string shown    = typed(kLogLines[i].text, n);
            m_text->DrawText(dc, stamp, left, y, px, DirectX::XMFLOAT4{kTextR, kTextG, kTextB, 0.35f * alpha});
            m_text->DrawText(dc, shown, bodyX, y, px,
                             isNewest ? DirectX::XMFLOAT4{kDotR, kDotG, kDotB, alpha}
                                      : DirectX::XMFLOAT4{kTextR, kTextG, kTextB, 0.55f * alpha});

            // 最新の行の末尾に、0.8 秒周期で点滅するカーソルを置く
            if (isNewest && std::fmod(time, 0.8f) < 0.4f)
            {
                m_text->DrawText(dc, "_", bodyX + m_text->MeasureWidth(shown, px), y, px,
                                 DirectX::XMFLOAT4{kDotR, kDotG, kDotB, alpha});
            }
        }
    }

    // ---------------------------------------------------------
    // 8. ヒント
    // ---------------------------------------------------------

    // 左下に白金の札「ヒント」を置き、その右に1行を打ち込む
    if (boot > 0.5f)
    {
        const float px   = 30.0f * k;
        const float tagW = 78.0f * k;
        const float tagH = 39.0f * k;
        DrawQuad(left + tagW * 0.5f, tipY + px * 0.5f, tagW, tagH, 0.0f, kDotR, kDotG, kDotB, alpha);

        const float tagPx = 24.0f * k;
        const float tw    = m_text->MeasureWidth("ヒント", tagPx);
        m_text->DrawText(dc, "ヒント", left + (tagW - tw) * 0.5f, tipY + (px - tagPx) * 0.5f, tagPx,
                         DirectX::XMFLOAT4{0.06f, 0.07f, 0.10f, alpha});

        m_text->DrawText(dc, typed(kTips[m_tipIndex], (boot - 0.5f) * 30.0f), left + tagW + 27.0f * k, tipY, px, textCol);
    }
}



void SceneLoading::OnDrawImGui()
{
}
