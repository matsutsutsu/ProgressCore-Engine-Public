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

    // 表示用の進捗。実際のロードは進捗量を取れない（Initialize の中は見えない）ので、見せかけで作る:
    //   ・ロード中は最低待機時間に比例して 0→90% まで伸ばす
    //   ・完了したら 100% を目標にする
    //   ・指数減衰で滑らかに追従させる
    const float target = done ? 1.0f
                              : std::min(0.9f, (time / kMinLoadTime) * 0.9f);
    m_progress += (target - m_progress) * CCL::Math::Damp(kProgressDamping, elapsedTime);

    // 裏読みが終わり、最低待機時間も過ぎたら本命シーンへ引き渡す。
    //   画面は SceneManager が「黒のまま保持」しているので、ローディング画面は見えていない。
    //   そのため「バー100%を見せてから」のような演出上の待ちは入れない。
    //   RequestTransition は黒の保持を解いて本命へ差し替え、フェードインを始める
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
        m_white = std::make_unique<Sprite>(device); // 白のダミーテクスチャ。色を掛けて単色矩形にする
    }

    // プログレスバー
    const float barW = sw * 0.5f; // 画面幅の半分
    const float barH = 16.0f;
    const float barX = (sw - barW) * 0.5f; // 中央寄せ
    const float barY = sh * 0.72f;

    // 背景（少し大きめの暗い矩形を枠の代わりにする）
    m_white->Render(dc, barX - 2.0f, barY - 2.0f, 0.0f, barW + 4.0f, barH + 4.0f, 0.0f,
                    0.08f, 0.08f, 0.10f, 0.85f);
    // フィル（進捗ぶんだけ幅を掛ける）
    m_white->Render(dc, barX, barY, 0.0f, barW * m_progress, barH, 0.0f,
                    0.20f, 0.65f, 1.0f, 1.0f);

    // テキスト
    if (m_text->IsReady())
    {
        const float px = 28.0f;

        // "NOW LOADING" + 経過時間に応じて 0〜3 個のドットを付ける（左寄せ）
        const int   dots = static_cast<int>(time * 2.0f) % 4;
        std::string msg  = "NOW LOADING";
        for (int i = 0; i < dots; ++i)
            msg += ".";
        m_text->DrawText(dc, msg, barX, barY - px - 10.0f, px,
                         DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f});

        // パーセンテージ（バーの右端に右寄せ）
        std::string pct = std::to_string(static_cast<int>(m_progress * 100.0f)) + "%";
        const float pw  = m_text->MeasureWidth("100%", px); // 最大幅で位置を固定し、桁数が変わっても揺れないようにする
        m_text->DrawText(dc, pct, barX + barW - pw, barY - px - 10.0f, px,
                         DirectX::XMFLOAT4{0.7f, 0.85f, 1.0f, 1.0f});
    }
}

void SceneLoading::OnDrawImGui()
{
}
