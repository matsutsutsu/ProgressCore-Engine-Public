#include "SceneLoading.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/RenderState.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Text/TextRenderer.h"
#include "Engine/Graphics/Renderer/Sprite.h"
#include "Engine/Core/Math/Interpolation.h" // Damp(): フレームレート非依存の追従係数

#include <objbase.h> // CoInitializeEx / CoUninitialize（ワーカースレッドの COM 初期化）
#include <string>
#include <algorithm> // std::min / std::clamp
#include <cmath>     // std::exp / std::sin
#include <cstdio>    // std::snprintf

namespace
{
    // 進捗が目標値へ追いつく速さ。裏読みが終わったときに、残りの目盛りが点ききるまでの速さになる
    constexpr float kProgressDamping = 8.0f;

    // ◆の色（HUD の白金に揃える）
    constexpr float kDotR = 0.96f, kDotG = 0.88f, kDotB = 0.62f;

    // ログの各行と、その行を出す進捗。読み込みの段階を書き、待っている間に作品の中身（AI・陣形・群れ）を伝える。
    //   6 行目（配備数）は数字が動くので、本文は描くときに作る
    struct LogLine
    {
        float       at;   // この進捗に届いたら出す
        const char *text; // 時刻の後ろに出す本文
    };
    constexpr LogLine kLogLines[kLogLineCount] = {
        {0.00f, "SYS   BOOT SEQUENCE INITIATED"},
        {0.08f, "SYS   MEMORY CHECK ... OK"},
        {0.18f, "NET   LINK TO SWARM CONTROLLER ... OK"},
        {0.30f, "AI    LOADING BEHAVIOR TREE : MURIEL"},
        {0.42f, "AI    FORMATION TABLE ... 32 PATTERNS"},
        {0.55f, nullptr}, // UNIT  DEPLOYING DRONES  xxxx / 1000
        {0.68f, "GFX   SHADER CACHE ... OK"},
        {0.80f, "SYS   SYNCHRONIZING ..."},
        {0.98f, "SYS   ALL SYSTEMS GREEN"},
    };

    // ロードのたびに1つ出すヒント。どれも実際の仕様に沿った内容にする
    //   （CleaveComponent: ジャスト回避でゲージ・ストック3＝チェイン3 / BossPhaseComponent: HP50% で第2形態）
    constexpr const char *kTips[] = {
        "JUST EVADE AN ATTACK TO CHARGE THE CLEAVE GAUGE.",
        "CLEAVE CUTS THROUGH THE SWARM. CHAIN IT UP TO THREE TIMES.",
        "THE SWARM'S SHAPE IS THE ATTACK. READ IT BEFORE IT STRIKES.",
        "BELOW HALF HP, MURIEL CHANGES HER TACTICS.",
    };
    constexpr int kTipCount = static_cast<int>(sizeof(kTips) / sizeof(kTips[0]));
    int           g_tipIndex = 0; // ロードのたびに次のヒントへ進める

    // 0〜1 の決まった乱数。◆ごとの半径・大きさの散らしや粒子ノイズに使う（同じ入力なら毎フレーム同じ値）
    float Hash(float x)
    {
        const float v = std::sin(x * 12.9898f) * 43758.5453f;
        return v - std::floor(v);
    }
} // namespace

SceneLoading::SceneLoading(Scene *nextScene) : m_nextScene(nextScene) {}

SceneLoading::~SceneLoading()
{
    // ロードジョブが走っていれば、完了（m_loadCounter が 0）を待ってから破棄する。
    // 通常は Finalize 済みですぐ返るが、Finalize を経ずに破棄された場合の安全弁
    SceneManager::Instance().GetLoaderJobs().WaitForCounter(&m_loadCounter);
}

void SceneLoading::Initialize()
{
    // 共通初期化（カメラ/リソース登録など）だけ行う。
    // SceneLoading は RegisterSystems を上書きしない（System 0個）ので軽い
    BaseScene::Initialize();

    m_isFinished = false;

    // 行き先のシーン名。SceneManager::ChangeSceneAsync がこのシーンを作る前に、行き先の名前を設定している
    m_destName = SceneManager::Instance().GetCurrentSceneName();
    m_tipIndex = g_tipIndex++ % kTipCount;
    m_logStart.fill(-1.0f);

    if (m_nextScene)
    {
        Scene             *target   = m_nextScene;
        std::atomic<bool> *finished = &m_isFinished;

        // 自作の JobSystem を「シーンの非同期ロード」にも使う。ここでは WaitForCounter で
        // 待たずに投げっぱなしにし、m_isFinished を毎フレーム覗く（fire-and-poll）。
        //   Execute(job, counter) … job はメインではなくワーカースレッドで実行される。
        //                           counter は Finalize / デストラクタで完了を待つために渡す
        SceneManager::Instance().GetLoaderJobs().Execute(
            [target, finished]() {
                // WIC 経由のテクスチャ読み込みには、このスレッドでの COM 初期化が要る。
                HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

                // ここで対象シーンの重いロードが走る。
                //   DeviceContext（描画コマンド）は触らないこと。
                //   リソース生成（CreateBuffer/CreateTexture）はスレッドセーフなので可
                target->Initialize();
                // release で立てるので、メインスレッドが acquire で true を見た時点で
                // Initialize が作った World の中身も全て見えている
                target->SetReady();

                if (SUCCEEDED(hr)) CoUninitialize();

                // 最後に完了を通知する（Update 側がこれを覗いて遷移する）
                finished->store(true, std::memory_order_release);
            },
            &m_loadCounter);
    }
}

void SceneLoading::Finalize()
{
    // ロードジョブが完了するまでブロックする（スレッドの join 相当）。
    // 裏読み中のまま this を壊すと、ジョブが m_isFinished に書き込めなくなるため
    SceneManager::Instance().GetLoaderJobs().WaitForCounter(&m_loadCounter);

    BaseScene::Finalize();
}

void SceneLoading::Update(float elapsedTime)
{
    time += elapsedTime;

    BaseScene::Update(elapsedTime);

    // ワーカースレッドの release と対になる acquire で読む
    const bool done = m_isFinished.load(std::memory_order_acquire);

    // 最低表示時間の間か（裏読みが終わっていても、ログが最後まで流れるよう kMinShowTime 秒までは表示する）
    const bool holding = time < kMinShowTime;

    // --- 表示用の進捗 ---
    //   実際のロードは進捗量を取れない（Initialize の中は見えない）ので、見せかけで作る。
    //   ロード中は 90% へ近づき続け（長くかかっても止まって見えない）、完了したら 100% へ追いつく。
    //   最低表示時間の間は、その時間いっぱいを使って 100% へ伸ばす
    float target = done ? 1.0f : 0.9f * (1.0f - std::exp(-time / kProgressTau));
    if (done && holding)
        target = std::min(1.0f, time / kMinShowTime);
    m_progress += (target - m_progress) * CCL::Math::Damp(kProgressDamping, elapsedTime);
    if (m_progress >= 0.98f) m_outroTime += elapsedTime; // 100% に届いた（指数的に近づくので 98% で届いたとみなす）

    // ログ: 進捗が各行の値に届いたら、その時刻を記録する（行は時刻から1文字ずつ打ち込む）。
    //   見出しが出そろう前には出さない
    for (int i = 0; i < kLogLineCount; ++i)
    {
        if (m_logStart[i] < 0.0f && m_progress >= kLogLines[i].at && time >= kShowDelay + 0.4f)
            m_logStart[i] = time;
    }

    // --- 表示の濃さ ---
    //   ロード中: 少し待ってから出す（すぐ終わるロードで一瞬だけ出ないように）
    //   完了後  : 出していれば、進捗が 100% に届いて完了の演出を見せてから消す。出していなければそのまま
    float visTarget = 0.0f;
    if (!done || holding)
        visTarget = (time >= kShowDelay) ? 1.0f : 0.0f;
    else
        visTarget = (m_visibility > 0.0f && m_outroTime < kOutroHold) ? 1.0f : 0.0f;

    const float step = kFadeSpeed * elapsedTime;
    m_visibility     = (visTarget > m_visibility) ? std::min(visTarget, m_visibility + step)
                                                  : std::max(visTarget, m_visibility - step);

    // 裏読みが終わり、最低待機時間が過ぎ、表示も消えたら本命シーンへ引き渡す。
    //   RequestTransition は黒の保持を解いて本命へ差し替え、フェードインを始める
    if (done && time >= kMinLoadTime && m_visibility <= 0.0f && m_nextScene)
    {
        if (SceneManager::Instance().RequestTransition(m_nextScene))
        {
            m_nextScene = nullptr;
        }
    }
}

void SceneLoading::RenderOverFade()
{
    if (m_visibility <= 0.0f) return;

    // TextRenderer / Sprite は自前で完結し ResourceManager のモデルに触れないため、
    // 裏でロード中でもメインスレッド単独で安全に描画できる
    auto         &gfx    = Graphics::Instance();
    auto         *dc     = gfx.GetDeviceContext();
    ID3D11Device *device = gfx.GetDevice();

    // 遅延生成（初回の描画時に一度だけ）
    if (!m_text)
    {
        m_text = std::make_unique<TextRenderer>();
        m_text->Initialize(device, "Assets/Fonts/RictyDiminished-Regular.ttf");
    }
    if (!m_white)
    {
        m_white = std::make_unique<Sprite>(device); // 白のダミーテクスチャ。色を掛けて単色矩形にする
    }

    // 幕の上に描くので、合成と深度は自分で入れる（幕が透明で描かれなかったフレームに備える）。
    //   合成は RT の α を書き換えない UITransparency。Transparency だと半透明の矩形が α の穴を開け、
    //   エディタの GameView でその部分だけ後ろのウィンドウが透ける
    dc->OMSetBlendState(gfx.GetRenderState()->GetBlendState(BlendState::UITransparency), nullptr, 0xffffffff);
    dc->OMSetDepthStencilState(gfx.GetRenderState()->GetDepthStencilState(DepthState::NoTestNoWrite), 0);

    // 線形のままだと出入りが機械的に見えるので、両端を緩やかにする
    const float alpha = CCL::Math::SmoothStep(m_visibility);
    DrawBackdrop(alpha);
    DrawScreen(alpha);
}

void SceneLoading::DrawQuad(float cx, float cy, float w, float h, float angle, float r, float g, float b, float a)
{
    // Sprite は左上で置き、矩形の中心で回すので、中心から左上へ戻して渡す
    m_white->Render(Graphics::Instance().GetDeviceContext(), cx - w * 0.5f, cy - h * 0.5f, 0.0f, w, h, angle,
                    r, g, b, a);
}

void SceneLoading::DrawBackdrop(float alpha)
{
    auto       &gfx = Graphics::Instance();
    auto       *dc  = gfx.GetDeviceContext();
    const float sw  = gfx.GetScreenWidth();
    const float sh  = gfx.GetScreenHeight();
    const float k   = sh / 1080.0f;

    // 四隅の色を変えた1枚の矩形を描く（頂点の間はなめらかに補間される）。順は 左上・右上・左下・右下
    auto gradient = [&](float x, float y, float w, float h, DirectX::XMFLOAT4 tl, DirectX::XMFLOAT4 tr,
                        DirectX::XMFLOAT4 bl, DirectX::XMFLOAT4 br) {
        const DirectX::XMFLOAT4 corners[4] = {tl, tr, bl, br};
        m_white->Render(dc, x, y, 0.0f, w, h, 0.0f, 0.0f, m_white->GetWidth(), m_white->GetHeight(), 0.0f, corners);
    };

    // --- 地: 真っ黒でなく、青みのある墨色。左上（文字のある側）を明るく、右下へ沈める ---
    gradient(0.0f, 0.0f, sw, sh,
             {0.150f, 0.155f, 0.175f, alpha}, {0.095f, 0.100f, 0.120f, alpha},
             {0.075f, 0.080f, 0.095f, alpha}, {0.035f, 0.038f, 0.050f, alpha});

    // --- 群れ: 画面の中央右で、◆の輪が回り続ける。四隅の文字の間を埋める、この画面の主役の絵 ---
    {
        constexpr float kTau = 6.2831853f;
        const float     cx   = sw * 0.64f;
        const float     cy   = sh * 0.50f;
        const float     R    = 330.0f * k;

        // 角度 a1 と a2 の差を -π〜π に収めて返す（輪をなめる光の距離に使う）
        auto angleDiff = [](float a1, float a2) {
            const float d = std::fmod(a1 - a2, kTau);
            return d > 3.1415927f ? d - kTau : (d < -3.1415927f ? d + kTau : d);
        };

        // ◆が回り、明るい光が輪を一周なめていく。1機ずつ半径をわずかに散らし、漂わせる
        const float   sweep = time * 1.1f;
        constexpr int kMain = 300;
        for (int i = 0; i < kMain; ++i)
        {
            const float fi    = static_cast<float>(i);
            const float a     = kTau * fi / kMain + time * 0.12f;
            const float drift = time * (0.6f + Hash(fi + 7.7f) * 0.6f) + fi;
            const float r     = R * (1.0f + (Hash(fi + 4.3f) - 0.5f) * 0.06f + std::sin(drift) * 0.008f);
            const float depth = 0.45f + 0.55f * Hash(fi + 1.9f); // 奥のものほど小さく暗い
            const float d     = angleDiff(a, sweep);
            const float glow  = std::exp(-d * d * 10.0f) * (d < 0.0f ? 1.0f : 0.35f); // 光の進む先は鋭く、通った後は尾を引く
            const float size  = (3.0f + 4.0f * depth + 2.0f * glow) * k;
            DrawQuad(cx + std::cos(a) * r, cy + std::sin(a) * r, size, size, 45.0f,
                     kDotR + (1.0f - kDotR) * glow, kDotG + (1.0f - kDotG) * glow, kDotB + (1.0f - kDotB) * glow,
                     std::min(1.0f, 0.20f + 0.30f * depth + 0.6f * glow) * alpha);
        }
    }

    // --- 周辺減光: 四辺から内側へ、なめらかに暗くする（段にすると内側に四角い枠が見えるので、頂点色で補間する） ---
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

    // --- 粒子ノイズ: 1秒に 24 回散らし直す細かな点。のっぺりした面にフィルムのような質感を足す ---
    {
        constexpr int kGrains = 1600;
        const float   seed    = std::fmod(std::floor(time * 24.0f), 64.0f) * 17.0f; // 大きな値で sin の精度が落ちないよう 64 通りで回す
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
    auto       &gfx = Graphics::Instance();
    auto       *dc  = gfx.GetDeviceContext();
    const float sw  = gfx.GetScreenWidth();
    const float sh  = gfx.GetScreenHeight();
    const float k   = sh / 1080.0f;

    // 色: 文字は少し温かみのある白、飾りの線は白金、標的（ボス）と完了の合図は深紅
    constexpr float kTextR = 0.93f, kTextG = 0.91f, kTextB = 0.86f;
    constexpr float kRedR = 0.95f, kRedG = 0.18f, kRedB = 0.22f;

    // --- 時計 ---
    //   boot: 表示が出始めてからの秒数（見出し・線・行き先が組み上がる）
    //   lock: 進捗が 100% に届いてからの完了の合図の進み具合 0〜1
    const float boot  = std::max(0.0f, time - kShowDelay);
    const float lock  = std::clamp(m_outroTime / 0.25f, 0.0f, 1.0f);
    const float lockE = 1.0f - (1.0f - lock) * (1.0f - lock);

    // 起動直後の 0.2 秒だけ、全体をちらつかせる
    if (boot < 0.2f && Hash(std::floor(time * 30.0f) + 0.5f) < 0.35f)
        alpha *= 0.3f;

    // --- 背景の方眼: 線は控えめにし、交点に小さな十字を置いて方眼だと読めるようにする ---
    const float grid  = 120.0f * k;
    const float cross = 9.0f * k; // 交点の十字の腕の長さ（端から端）
    for (float x = grid; x < sw; x += grid)
        DrawQuad(x, sh * 0.5f, 1.0f, sh, 0.0f, kDotR, kDotG, kDotB, 0.2f * alpha);
    for (float y = grid; y < sh; y += grid)
        DrawQuad(sw * 0.5f, y, sw, 1.0f, 0.0f, kDotR, kDotG, kDotB, 0.2f * alpha);
    for (float x = grid; x < sw; x += grid)
    {
        for (float y = grid; y < sh; y += grid)
        {
            DrawQuad(x, y, cross, 1.0f, 0.0f, kDotR, kDotG, kDotB, 0.20f * alpha);
            DrawQuad(x, y, 1.0f, cross, 0.0f, kDotR, kDotG, kDotB, 0.20f * alpha);
        }
    }

    // 完了した瞬間、画面全体を深紅に一瞬だけ光らせる
    if (lock > 0.0f)
        DrawQuad(sw * 0.5f, sh * 0.5f, sw, sh, 0.0f, kRedR, kRedG, kRedB, 0.12f * (1.0f - lock) * alpha);

    const float left = 96.0f * k; // 左の余白。見出し・ログ・ヒントの左端をここにそろえる

    // --- 見出しの下の線: 左から引かれ、右端に◆ ---
    const float headPx = 96.0f * k;
    const float headY  = 72.0f * k;
    const float ruleY  = headY + headPx + 18.0f * k;
    const float ruleW  = 760.0f * k;
    {
        const float t    = std::clamp((boot - 0.15f) / 0.35f, 0.0f, 1.0f);
        const float draw = 1.0f - (1.0f - t) * (1.0f - t);
        if (draw > 0.0f)
        {
            DrawQuad(left + ruleW * draw * 0.5f, ruleY, ruleW * draw, 2.0f * k, 0.0f, kDotR, kDotG, kDotB, 0.7f * alpha);
            DrawQuad(left + ruleW * draw, ruleY, 7.0f * k, 7.0f * k, 45.0f, kDotR, kDotG, kDotB, alpha);
        }
    }

    // --- 右下の照準リング: 進捗の分だけ目盛りが点き、100% で縮んで深紅に染まる ---
    {
        const float cx    = sw - 170.0f * k;
        const float cy    = sh - 175.0f * k;
        const float R     = 66.0f * k * (1.0f - 0.15f * lockE);
        const float ringR = kDotR + (kRedR - kDotR) * lockE;
        const float ringG = kDotG + (kRedG - kDotG) * lockE;
        const float ringB = kDotB + (kRedB - kDotB) * lockE;

        constexpr int kTicks = 48;
        for (int i = 0; i < kTicks; ++i)
        {
            const float u   = static_cast<float>(i) / kTicks;
            const float a   = 6.2831853f * u + time * 0.35f;
            const bool  lit = u < m_progress;
            // 目盛りの長い辺を半径の向きにそろえる（縦長の矩形を、角度 + 90 度回す）
            DrawQuad(cx + std::cos(a) * R, cy + std::sin(a) * R, 3.0f * k, (lit ? 13.0f : 7.5f) * k,
                     a * 57.29578f + 90.0f, ringR, ringG, ringB, (lit ? 0.95f : 0.18f) * alpha);
        }
        for (int arc = 0; arc < 3; ++arc)
        {
            for (int j = 0; j < 6; ++j)
            {
                const float a = (120.0f * arc + 7.0f * j) * 0.0174533f - time * 0.8f;
                DrawQuad(cx + std::cos(a) * R * 0.72f, cy + std::sin(a) * R * 0.72f, 3.0f * k, 4.5f * k,
                         a * 57.29578f + 90.0f, ringR, ringG, ringB, 0.6f * alpha);
            }
        }

        if (m_text && m_text->IsReady())
        {
            // リングの内側に進捗（桁数が変わっても位置が揺れないよう、最大幅で測る）
            const float px = 22.0f * k;
            char        buf[16];
            std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(m_progress * 100.0f + 0.5f));
            const float w = m_text->MeasureWidth(buf, px);
            m_text->DrawText(dc, buf, cx - w * 0.5f, cy - px * 0.55f, px, DirectX::XMFLOAT4{ringR, ringG, ringB, alpha});

            // リングの下: 読み込み中は NOW LOADING、完了したら点滅する READY
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

    // --- ヒントの上の区切り線 ---
    const float tipY = sh - 150.0f * k;
    {
        const float t = std::clamp((boot - 0.4f) / 0.35f, 0.0f, 1.0f);
        if (t > 0.0f)
            DrawQuad(left + ruleW * t * 0.5f, tipY - 30.0f * k, ruleW * t, 1.0f, 0.0f, kDotR, kDotG, kDotB, 0.3f * alpha);
    }

    if (!m_text || !m_text->IsReady()) return;

    // 文字列の先頭から n 文字だけを返す（1文字ずつ打ち込む表示）
    auto typed = [](const std::string &s, float n) {
        return s.substr(0, static_cast<size_t>(std::clamp(n, 0.0f, static_cast<float>(s.size()))));
    };
    const DirectX::XMFLOAT4 textCol{kTextR, kTextG, kTextB, alpha};

    // --- 見出し: 左から滑り込む大きな LOADING と、順に点く3つの四角 ---
    {
        const float t     = std::clamp(boot / 0.35f, 0.0f, 1.0f);
        const float slide = (1.0f - t) * (1.0f - t) * 40.0f * k; // 残りの距離（減速しながら止まる）
        m_text->DrawText(dc, "LOADING", left - slide, headY, headPx, DirectX::XMFLOAT4{kTextR, kTextG, kTextB, t * alpha});

        const float dotsX = left - slide + m_text->MeasureWidth("LOADING", headPx) + 28.0f * k;
        const int   step  = static_cast<int>(time * 3.0f) % 4; // 0〜3 個が点く
        for (int i = 0; i < 3; ++i)
        {
            DrawQuad(dotsX + 20.0f * k * i, headY + headPx * 0.72f, 10.0f * k, 10.0f * k, 0.0f,
                     kDotR, kDotG, kDotB, (i < step ? 1.0f : 0.2f) * t * alpha);
        }
    }

    // --- 行き先（線の下）: 何を読み込んでいるかを言葉で出す ---
    {
        const float px = 18.0f * k;
        const float y  = ruleY + 14.0f * k;
        const float n  = (boot - 0.3f) * 60.0f; // 1秒に 60 文字

        std::string head;
        std::string target;
        if (m_destName == "Game")
        {
            head   = "SORTIE  //  TARGET : ";
            target = "MURIEL";
        }
        else if (m_destName == "Title")
        {
            head = "RETURN  //  TITLE SCREEN";
        }
        else
        {
            head = "TRANSFER  //  " + m_destName;
        }

        m_text->DrawText(dc, typed(head, n), left, y, px, DirectX::XMFLOAT4{kTextR, kTextG, kTextB, 0.8f * alpha});
        if (!target.empty())
        {
            m_text->DrawText(dc, typed(target, n - static_cast<float>(head.size())),
                             left + m_text->MeasureWidth(head, px), y, px, DirectX::XMFLOAT4{kRedR, kRedG, kRedB, alpha});
        }
    }

    // --- ログ: 出た時刻・本文の順に、1行ずつ打ち込む。最新の行だけ白金で強調し、古い行は沈める ---
    {
        const float px     = 20.0f * k;
        const float lineH  = 32.0f * k;
        const float top    = ruleY + 70.0f * k;
        const float bodyX  = left + m_text->MeasureWidth("00.00  ", px);
        int         newest = -1;
        for (int i = 0; i < kLogLineCount; ++i)
            if (m_logStart[i] >= 0.0f) newest = i;

        for (int i = 0; i <= newest; ++i)
        {
            if (m_logStart[i] < 0.0f) continue;

            const float y = top + lineH * static_cast<float>(i);
            const float n = (time - m_logStart[i]) * 60.0f; // 1秒に 60 文字

            char stamp[16];
            std::snprintf(stamp, sizeof(stamp), "%05.2f", std::max(0.0f, m_logStart[i] - kShowDelay));

            std::string body;
            if (kLogLines[i].text)
            {
                body = kLogLines[i].text;
            }
            else
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "UNIT  DEPLOYING DRONES  %04d / 1000",
                              std::min(1000, static_cast<int>(m_progress * 1000.0f)));
                body = buf;
            }

            const bool        isNewest = (i == newest);
            const std::string shown    = typed(body, n);
            m_text->DrawText(dc, stamp, left, y, px, DirectX::XMFLOAT4{kTextR, kTextG, kTextB, 0.35f * alpha});
            m_text->DrawText(dc, shown, bodyX, y, px,
                             isNewest ? DirectX::XMFLOAT4{kDotR, kDotG, kDotB, alpha}
                                      : DirectX::XMFLOAT4{kTextR, kTextG, kTextB, 0.55f * alpha});

            // 最新の行の末尾に、点滅するカーソルを置く
            if (isNewest && std::fmod(time, 0.8f) < 0.4f)
                m_text->DrawText(dc, "_", bodyX + m_text->MeasureWidth(shown, px), y, px,
                                 DirectX::XMFLOAT4{kDotR, kDotG, kDotB, alpha});
        }
    }

    // --- ヒント（左下）: 白金の札「TIP」と、その右に1行 ---
    if (boot > 0.5f)
    {
        const float px   = 20.0f * k;
        const float tagW = 52.0f * k;
        const float tagH = 26.0f * k;
        DrawQuad(left + tagW * 0.5f, tipY + px * 0.5f, tagW, tagH, 0.0f, kDotR, kDotG, kDotB, alpha);

        const float tagPx = 16.0f * k;
        const float tw    = m_text->MeasureWidth("TIP", tagPx);
        m_text->DrawText(dc, "TIP", left + (tagW - tw) * 0.5f, tipY + (px - tagPx) * 0.5f, tagPx,
                         DirectX::XMFLOAT4{0.06f, 0.07f, 0.10f, alpha});

        m_text->DrawText(dc, typed(kTips[m_tipIndex], (boot - 0.5f) * 70.0f), left + tagW + 18.0f * k, tipY, px, textCol);
    }
}

void SceneLoading::OnDrawImGui()
{
}
