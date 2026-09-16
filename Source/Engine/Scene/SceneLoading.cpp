#include "SceneLoading.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/UI/Text/TextRenderer.h"
#include "Engine/Graphics/Renderer/Sprite.h"
#include "Engine/Core/Math/Interpolation.h" // Damp(): フレームレート非依存の追従係数

#include <objbase.h> // CoInitializeEx / CoUninitialize（ワーカースレッドの COM 初期化）
#include <string>
#include <algorithm> // std::min
#include <cmath>     // std::exp

namespace
{
    // 進捗バーが目標値へ追いつく速さ。大きいほどキビキビ動くが、
    // 速すぎると読み込みが一瞬で終わったように見えて不安を与える。
    constexpr float kProgressDamping = 8.0f;
} // namespace

SceneLoading::SceneLoading(Scene *nextScene) : m_nextScene(nextScene) {}

SceneLoading::~SceneLoading()
{
    // 念のため：ロードジョブが走っていれば完了を待ってから破棄する。
    // （通常は Finalize 済みで即返るが、二重の安全弁。）
    // loadCounterでワーカースレッドの仕事のカウントが０になるまで待つ
    SceneManager::Instance().GetLoaderJobs().WaitForCounter(&m_loadCounter);
}

void SceneLoading::Initialize()
{
    // 共通初期化（カメラ/リソース登録など）。重い処理はここでは行わない。
    // ※ SceneLoading は RegisterSystems を持たない＝システム0個なので軽い。
    BaseScene::Initialize();

    m_isFinished = false;

    if (m_nextScene)
    {
        Scene             *target   = m_nextScene;
        std::atomic<bool> *finished = &m_isFinished;

        // 自作の汎用 JobSystem を「フレーム並列」だけでなく
        // 「シーン非同期ロード」にも使い回す。ここでは WaitForCounter で
        // 待たずに投げっぱなしにし、m_isFinished を毎フレーム覗く（fire-and-poll）。
        // Executeの中身はメインではなくワーカースレッドで実行される、これが非同期ロード
        // Execute(job, counter) ... Execute(ワーカースレッドで実行する処理, カウンター)
        SceneManager::Instance().GetLoaderJobs().Execute(
            [target, finished]() {
                // WIC 経由のテクスチャ読み込みには、このスレッドでの COM 初期化が要る。
                HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

                // ここで対象シーンの重いロードが走る。
                //    DeviceContext（描画コマンド）は触らないこと。
                //    リソース生成（CreateBuffer/CreateTexture）はフリースレッドなので可。
                target->Initialize();
                // SetReadyで中身のisReadyをtrueにする、これで他のスレッドが正しい値を確認できる
                target->SetReady(); // release：以降 world の中身がメインから安全に見える

                if (SUCCEEDED(hr)) CoUninitialize();

                // 最後に完了を通知（Update 側がこれを覗いて遷移する）
                finished->store(true, std::memory_order_release);
            },
            &m_loadCounter);
    }
}

void SceneLoading::Finalize()
{
    // ロードジョブが完了するまでブロック（join 相当）。
    // これ以降 m_nextScene の所有権は SceneManager 側に渡っている。
    SceneManager::Instance().GetLoaderJobs().WaitForCounter(&m_loadCounter);

    BaseScene::Finalize();
}

void SceneLoading::Update(float elapsedTime)
{
    time += elapsedTime;

    BaseScene::Update(elapsedTime);

    // ワーカースレッドで書き込まれたデータを読み込む、acquireでしっかりと正しい値を確認できる
    const bool done = m_isFinished.load(std::memory_order_acquire);

    // --- 表示用進捗のスムージング ---
    // 実ロードは進捗量を取れない（Initialize はブラックボックス）ため「見せかけ」で作る:
    //   ・ロード中は最低表示時間に比例して 0→90% まで伸ばす（作業中の演出）
    //   ・完了したら 100% を目標にする
    //   ・指数減衰でヌルっと追従させる
    const float target = done ? 1.0f
                              : std::min(0.9f, (time / kMinLoadTime) * 0.9f);
    m_progress += (target - m_progress) * CCL::Math::Damp(kProgressDamping, elapsedTime);

    // --- 遷移条件 ---
    //   ① 裏読み完了（done） かつ
    //   ② 最低表示時間を経過（一瞬で終わってもロード画面を必ず見せる） かつ
    //   ③ バーが満タン付近（100% を見せてから切り替える）
    // 裏読みが終わり次第、本命シーンへ引き渡す。
    //   画面は SceneManager が「黒のまま保持」しているため、ローディング画面は
    //   見えていない。よって「最低表示時間」や「バー100%待ち」の演出条件は不要で、
    //   完了したら即引き渡してよい（黒→ゲームの1回フェードインにつながる）。
    //   RequestTransition は黒保持を解いて本命へ差し替え、フェードインを開始する。
    if (done && time >= kMinLoadTime && m_nextScene)
    {
        if (SceneManager::Instance().RequestTransition(m_nextScene))
        {
            m_nextScene = nullptr;
        }
    }
}

void SceneLoading::OnRenderUI()
{
    // BaseScene::Render の2Dパス内（blend/depth 設定済み）から呼ばれる。
    // TextRenderer / 白Sprite は自前で完結し ResourceManager のモデルに触れないため、
    // 裏でロード中でもメインスレッド単独で安全に描画できる。
    auto         &gfx    = Graphics::Instance();
    auto         *dc     = gfx.GetDeviceContext();
    ID3D11Device *device = gfx.GetDevice();
    const float   sw     = gfx.GetScreenWidth();
    const float   sh     = gfx.GetScreenHeight();

    // 遅延生成（初回の描画時に一度だけ）
    if (!m_text)
    {
        m_text = std::make_unique<TextRenderer>();
        m_text->Initialize(device, "Assets/Fonts/RictyDiminished-Regular.ttf");
    }
    if (!m_white)
    {
        m_white = std::make_unique<Sprite>(device); // 白ダミーテクスチャ。色を掛けて単色矩形に
    }

    // ================= プログレスバー =================
    const float barW = sw * 0.5f; // 画面幅の半分
    const float barH = 16.0f;
    const float barX = (sw - barW) * 0.5f; // 中央寄せ
    const float barY = sh * 0.72f;

    // 背景（少し大きめの暗い矩形＝枠代わり）
    m_white->Render(dc, barX - 2.0f, barY - 2.0f, 0.0f, barW + 4.0f, barH + 4.0f, 0.0f,
                    0.08f, 0.08f, 0.10f, 0.85f);
    // フィル（進捗ぶんだけ幅を掛ける）
    m_white->Render(dc, barX, barY, 0.0f, barW * m_progress, barH, 0.0f,
                    0.20f, 0.65f, 1.0f, 1.0f);

    // ================= テキスト =================
    if (m_text->IsReady())
    {
        const float px = 28.0f;

        // "NOW LOADING" ＋ time に応じて 0..3 個のドットを点滅（左寄せ）
        const int   dots = static_cast<int>(time * 2.0f) % 4;
        std::string msg  = "NOW LOADING";
        for (int i = 0; i < dots; ++i)
            msg += ".";
        m_text->DrawText(dc, msg, barX, barY - px - 10.0f, px,
                         DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f});

        // パーセンテージ（バー右端に右寄せ）
        std::string pct = std::to_string(static_cast<int>(m_progress * 100.0f)) + "%";
        const float pw  = m_text->MeasureWidth("100%", px); // 右端固定で桁揺れを防ぐ
        m_text->DrawText(dc, pct, barX + barW - pw, barY - px - 10.0f, px,
                         DirectX::XMFLOAT4{0.7f, 0.85f, 1.0f, 1.0f});
    }
}

void SceneLoading::OnDrawImGui()
{
}
