#include <memory>
#include <sstream>
#include <algorithm>
#include <string>
#include <cstring>
#include <filesystem>
#include <intrin.h>
#include <thread>
#include <windowsx.h>

#include "Framework.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Input/Input.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Core/App/GameFlowState.h"
#include "Engine/Serialization/Serializer/LevelSerializer.h"
#include "Engine/Scene/BaseScene.h"

// APIファイルをインクルード
#include "Engine/Audio/DirectXTKIntegration/DxtkAudioManager.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Platform/Input/InputFacade.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/LogSinks.h"
#include "Engine/Platform/Core/FileSystem.h"

#include "EngineServices.h"
#include "Engine/ECS/Jobs/CCL_JobSystem.h"
#include "Engine/Core/Components/RenderInterpState.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Core/EditorWindowManager.h"
#include "Editor/Core/EditorCommandHistory.h"
#include "Editor/Core/EditorContext.h"
#include "Editor/Windows/General/ConsoleWindow.h"
#include "Engine/Graphics/Renderer/ImGuiRenderer.h"
#include "Engine/Graphics/Renderer/Core/RenderDebugRegistry.h"
#endif

#include "tracy/Tracy.hpp"
#include "tracy/TracyD3D11.hpp"
#include "Engine/Core/Profiling/TracySection.h"

// Present の垂直同期間隔（1 = 毎 VSync で表示）
static const int syncInterval = 1;

Framework::Framework(HWND hWnd)
    : hWnd(hWnd)
{
    // 処理フロー:
    // 1. ログ、ファイルシステム（Shipping ではアーカイブを開く）
    // 2. 描画（と、エディタ構成では ImGui・ファイルダイアログ）
    // 3. 入力
    // 4. 共有サービス（ジョブプール・入力設定・オーディオ・物理）
    // 5. シーン
    // 6. エディタのウィンドウ

    // 1 は必ず最初にする。2 以降のシステムが初期化中に CCL_LOG を呼ぶため
    // 4 の中では物理がジョブプールを使うので、プールを先に作る

    ZoneScopedN("Framework::Constructor");

    {
        ZoneScopedN("Init: Logger");
        Logger::Instance().AddSink(std::make_shared<DebugStreamSink>());

        // ログファイルの出力先。開発時は Build/ 配下に集約し、製品（Shipping）は exe の隣の Logs/ に置く

        // Shipping でも起動時に実行環境（LogPlatformInfo・GraphicsDevice）を書くので、毎回ファイルが作られる
        // それ以降に書かれるのは ERROR だけ
        // 起動ごとに前回分を Engine.prev.log へ退避する2世代式
#ifdef ENABLE_EDITOR
        const char *kLogPath = "Build/Logs/Engine.log";
#else
        const char *kLogPath = "Logs/Engine.log";
#endif
        Logger::Instance().AddSink(std::make_shared<FileSink>(kLogPath));
        m_consoleLogSink = std::make_shared<ImGuiBufferSink>(2000);
        Logger::Instance().AddSink(m_consoleLogSink);

        LogPlatformInfo();
    }

    {
        // 2 以降はアセットを読むので、その前にアーカイブを開いておく（Shipping のみ。ログを使うので 1 の後）
        ZoneScopedN("Init: FileSystem");
        FileSystem::Initialize();
    }

    {
        ZoneScopedN("Init: Graphics");
        Graphics::Instance().Initialize(hWnd);

        // 実行環境の報告（LogPlatformInfo と GraphicsDevice の GPU）はここで出そろう
        // ERROR 以外はファイルへすぐ書かれないので、固まって強制終了されても残るよう、ここで書き出す
        Logger::Instance().Flush();
    }
#ifdef ENABLE_EDITOR
    {
        ZoneScopedN("Init: ImGuiRenderer");
        ImGuiRenderer::Initialize(hWnd, Graphics::Instance().GetDevice(), Graphics::Instance().GetDeviceContext());
    }
#endif
    // FileDialog を一括初期化する。プリセットの登録と、前回の履歴の読み込み（Build/User/EditorPrefs.json）がここで完結する

    // ネイティブのファイル選択ダイアログはエディタの機能なので、Shipping では初期化しない（EditorPrefs.json も作らない）
#ifdef ENABLE_EDITOR
    {
        ZoneScopedN("Init: FileDialog");
        FileDialog::Initialize();
    }
#endif

    {
        ZoneScopedN("Init: Input");
        Input::Instance().Initialize(hWnd);
    }

    // エンジン全体で共有するサービスを生成し、EngineServices に預ける
    auto &services = EngineServices::Instance();

    {
        ZoneScopedN("Init: JobSystem (shared frame pool)");

        // ECS のフレーム並列で全 World が共有する唯一のジョブプールを、ここ（上層）で1つだけ生成する
        // 以降 SystemManager はこれを注入して使う
        // スレッドプールはハードウェア資源（1コアに1ワーカー）なので、World / SystemManager ごとには持たない

        // シーンの非同期ロード用のプールは別にしている（SceneManager が持つ）
        // 時間のかかるロードのジョブを、フレーム並列の WaitForCounter がワークスティーリングで拾い、
        // フレームが止まるのを防ぐため
        services.jobSystem = std::make_shared<CCL::ECS::JobSystem>();
    }

    {
        ZoneScopedN("Init: InputFacade + LoadConfig");
        services.inputFacade = std::make_shared<InputFacade>(&Input::Instance());
        services.inputFacade->LoadConfig("Assets/Config/InputConfig.json");
    }

    {
        ZoneScopedN("Init: Audio (DXTK) + LoadConfig");
        services.audioManager = std::make_shared<DxtkAudioManager>();
        services.audioManager->Initialize();
        services.audioManager->LoadConfig("Assets/Config/AudioConfigDXTK.json");
    }

    {
        ZoneScopedN("Init: Physics (Jolt)");
        services.physicsManager = std::make_shared<JoltPhysicsManager>();

        // 共有スレッドプールを渡すと、物理ジョブも ECS と同じプールで走る（JoltJobSystemAdapter 経由）
        // プールが1本になり、物理ジョブが Tracy にジョブ名付きのゾーンとして表示される

        // Jolt 純正のプールを使いたい場合は、引数を外して Initialize() と呼ぶだけでよい
        // 比較計測のために両方の経路を残してある
        services.physicsManager->Initialize(services.jobSystem.get());
    }

    // この中で最初のシーンの生成予約まで行う
    {
        ZoneScopedN("Init: SceneManager");
        SceneManager::Instance().Initialize();
    }

#ifdef ENABLE_EDITOR
    // エディタの初期化は Engine 層ではなくフレームワーク層で行う
    {
        ZoneScopedN("Init: EditorWindowManager + LoadLayout");
        m_editorWindowManager = std::make_unique<EditorWindowManager>();
        m_editorWindowManager->Initialize();
        m_editorWindowManager->LoadLayout("Build/User/WindowLayout.json");

        // ConsoleWindow がログを表示できるよう、共有のシンクを渡す
        if (auto *console = m_editorWindowManager->GetWindow<ConsoleWindow>())
            console->SetBufferSink(m_consoleLogSink);
    }
#endif
}



Framework::~Framework()
{
    // 処理フロー:
    // 1. エディタのウィンドウ（レイアウトを保存してから）
    // 2. シーン
    // 3. ImGui とファイルダイアログの履歴（エディタ構成のみ）
    // 4. ログ
    // 5. 描画

    // 作ったときとおおむね逆の順に壊す。エディタはシーンの World を指しているので、シーンより先に壊す
    // 描画は最後にする。ResourceManager のクリアと、解放漏れの GPU オブジェクトの報告がここで行われるため

    ZoneScopedN("Framework::Destructor");

#ifdef ENABLE_EDITOR
    m_editorWindowManager->SaveLayout("Build/User/WindowLayout.json");
    // unique_ptr なので自動でも解放されるが、シーンより先に壊す順序を明示する
    m_editorWindowManager.reset();
#endif

    {
        ZoneScopedN("Finalize: SceneManager");
        SceneManager::Instance().Clear();
    }

#ifdef ENABLE_EDITOR
    {
        ZoneScopedN("Finalize: ImGuiRenderer");
        ImGuiRenderer::Finalize();
    }

    {
        ZoneScopedN("Finalize: SaveFileDialogHistory");
        // エディタ専用。Shipping では初期化もしないので保存もしない
        FileDialog::SaveHistory();
    }
#endif

    {
        ZoneScopedN("Finalize: Logger");
        Logger::Instance().Flush();
        Logger::Instance().ClearSinks(); // shared_ptr 循環参照防止
    }

    {
        ZoneScopedN("Finalize: Graphics::Shutdown");
        // 描画パイプラインを最後に破棄する（ResourceManager のクリアもここで行われる）
        // デバッグビルドでは、ここで解放漏れの GPU オブジェクトが ReportLiveDeviceObjects で出力される
        Graphics::Instance().Shutdown();
    }
}



void Framework::FixedUpdate(float fixedTime)
{
    ZoneScopedN("Framework::FixedUpdate");

    // 一時停止/コマ送りの判断は Run() 側で一元管理する
    //   ここが呼ばれた時点で「1物理ステップ進めてよい」ことは確定しているため、
    //   本関数は無条件にロジックを進める（停止中は Run() が本関数を呼ばない）
    SceneManager::Instance().FixedUpdate(fixedTime);

    // このステップで消費した「押した瞬間」系の入力フラグを下ろす
    Input::Instance().ClearLogicInput();
}



void Framework::Update(float elapsedTime)
{
    ZoneScopedN("Framework::Update");

#ifdef ENABLE_EDITOR
    {
        ZoneScopedN("Framework::ImGuiRenderer::NewFrame");
        ImGuiRenderer::NewFrame();
    }
#endif

    {
        ZoneScopedN("Framework::SceneManager::Update");
        SceneManager::Instance().Update(elapsedTime);
    }

#ifdef ENABLE_EDITOR
    {
        ZoneScopedN("Framework::EditorUpdate");

        // シーンが切り替わったら、前のシーンを指していたエディタ側の参照をすべて捨てる
        // Undo の履歴は World* と EntityID を持つので、残したまま Ctrl+Z を押すと解放済みのメモリに書く。選択も同じ
        // SceneManager は Engine の層で Editor を include できないので、切り替えは通し番号で受け取り、ここで捨てる
        {
            const uint32_t serial = SceneManager::Instance().GetSceneSerial();
            if (serial != m_lastSceneSerial)
            {
                m_lastSceneSerial = serial;
                EditorCommandHistory::Instance().Clear();
                m_editorContext.ClearSelection();
            }
        }

        // エディタが今のシーンの World / SystemManager を見られるよう、毎フレーム渡し直す
        auto *currentScene = SceneManager::Instance().GetCurrentScene();
        if (currentScene)
        {
            m_editorContext.world         = currentScene->GetWorld();
            m_editorContext.systemManager = currentScene->GetSystemManager();
        }
        else
        {
            m_editorContext.world         = nullptr;
            m_editorContext.systemManager = nullptr;
        }
    }
#endif
}



void Framework::Render(float elapsedTime)
{
    // 処理フロー:
    // 1. 前フレームの GPU 計測結果を回収する
    // 2. 描画先を選んでクリアする
    // 3. シーンを描く
    // 4. エディタの UI を描く（エディタ構成のみ）
    // 5. Present し、失敗したらデバイスを作り直す

    // Tracy の GPU タイムスタンプ回収はフレーム先頭で行う
    // 前フレームの Present / Flush から最も時間が経っており、
    // GPU 側の書き込みが完了している可能性が最も高いため
    if (auto tracyCtx = Graphics::Instance().GetTracyContext())
    {
        TracyD3D11Collect(tracyCtx);
    }

    ZoneScopedN("Framework::Render");

    ID3D11DeviceContext *dc = Graphics::Instance().GetDeviceContext();

    // ビルド構成で描画先を切り替える
#ifdef ENABLE_EDITOR
    // エディタあり（Development）: ゲームは裏画面（gameRTV）に描き、エディタの GameView に貼る
    bool useGameRTV = true;
#else
    // エディタなし（Shipping）: ゲームは直接バックバッファに描く
    bool useGameRTV = false;
#endif

    Graphics::Instance().Clear(0.5f, 0.5f, 0.5f, 1, useGameRTV);
    Graphics::Instance().SetRenderTargets(useGameRTV);

    {
        ZoneScopedN("SceneManager::Render");

        SceneManager::Instance().Render();
    }

#ifdef ENABLE_EDITOR
    {
        ZoneScopedN("Framework::Clear SetRenderTargets");

        // ここが CPU 側で重く見えるときは、この処理自体ではなく
        // GPU の描画完了を CPU が待たされていることが多い

        // エディタ UI はバックバッファへ描くので、描画先を戻す
        Graphics::Instance().Clear(0.1f, 0.1f, 0.1f, 1.0f, false);
        Graphics::Instance().SetRenderTargets(false);
    }

    {
        ZoneScopedN("FrameWork::ImGui");

        {
            ZoneScopedN("FrameWork::SceneManager::DrawGUI");
            SceneManager::Instance().DrawGUI();
        }

        {
            ZoneScopedN("FrameWork::EditorUI::Render");

            // ゲーム UI の後に、覆いかぶさるようにエディタ UI を描く
            if (m_editorWindowManager)
            {
                // 完成したゲーム画面のテクスチャを GameView に渡す
                m_editorContext.gameSRV = (void *)Graphics::Instance().GetGameSRV();

                // デバッグ表示（Render Debug ウィンドウ）へ device レベルの SRV を登録する
                // パス出力ではないため、SRV が確定するこの位置（描画完了後）で申告する
                {
                    const uint32_t sw = static_cast<uint32_t>(Graphics::Instance().GetScreenWidth());
                    const uint32_t sh = static_cast<uint32_t>(Graphics::Instance().GetScreenHeight());
                    RenderDebugRegistry::Instance().Register({"GameView (Final)", Graphics::Instance().GetGameSRV(), sw, sh, DebugTexKind::Color});
                    RenderDebugRegistry::Instance().Register({"SceneDepth", Graphics::Instance().GetDepthSRV(), sw, sh, DebugTexKind::Depth});
                }

                m_editorWindowManager->Draw(m_editorContext);

                if (m_editorWindowManager->IsVisible())
                {
                    m_editorContext.systemManager->OnGui();
                }
            }
        }

        {
            ZoneScopedN("FrameWork::ImGuiRenderer::Render");

            // GPU 側の ImGui 処理時間も計測する
            if (auto tracyCtx = Graphics::Instance().GetTracyContext())
            {
                TracyD3D11Zone(tracyCtx, "ImGui Pass (GPU)");
            }

            // 切り離した OS 窓（セカンダリ Present）の描画可否。UI表示中かつユーザーが
            // 有効にしている間は常に描画する（プレイ中でも別画面で操作可）。重い場面は
            // context.renderExternalWindows を false にして止められる
            const bool allowPlatformWindows =
                m_editorWindowManager &&
                m_editorWindowManager->IsVisible() &&
                m_editorContext.renderExternalWindows;
            ImGuiRenderer::SetPlatformWindowsEnabled(allowPlatformWindows);
            ImGuiRenderer::SetPlatformWindowsThrottled(m_editorContext.throttleExternalWindows);

            ImGuiRenderer::Render(dc);
        }
    }
#endif

    {
        // GPU の描画待ち（VSync 同期）の時間はここに乗る
        ZoneScopedN("Graphics::Present (VSync Wait)");

        if (Graphics::Instance().Present(syncInterval))
        {
            // 成功時のみ GPU のキューを押し出す
            if (dc) dc->Flush();
        }
        else
        {
            // 失敗時（デバイスロスト等）は Flush や Collect を呼んではいけない
            // GPU リソースを作り直し、シーンにも作り直しを知らせる
            Graphics::Instance().RecoverFromDeviceLost();
            if (auto *scene = dynamic_cast<BaseScene *>(SceneManager::Instance().GetCurrentScene()))
                scene->OnDeviceRecreated();
        }
    }
}



void Framework::LogPlatformInfo() const
{
    // --- どの exe か ---
    // 配布した版と、相手が動かした版が同じかを確かめるため
    CCL_LOG_ALWAYS(LogCategory::Core, "[環境] ビルド: %s %s", __DATE__, __TIME__);

    // --- OS ---
    // GetVersionEx は互換モードで古い版を返すことがあるので、ntdll の RtlGetVersion で本当の版を取る
    using RtlGetVersionFn = LONG(WINAPI *)(OSVERSIONINFOW *);
    OSVERSIONINFOW os{};
    os.dwOSVersionInfoSize = sizeof(os);
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
        fn(&os);
    CCL_LOG_ALWAYS(LogCategory::Core, "[環境] Windows %lu.%lu (build %lu)", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);

    // --- CPU ---
    // exe は AVX2 を使う設定でビルドしているので、AVX2 のない CPU では動かない
    int  regs[4]    = {};
    char brand[49]  = {};
    __cpuid(regs, 0x80000000);
    if (static_cast<unsigned>(regs[0]) >= 0x80000004)
    {
        for (int i = 0; i < 3; ++i)
        {
            __cpuid(regs, 0x80000002 + i);
            std::memcpy(brand + i * 16, regs, sizeof(regs));
        }
    }
    constexpr DWORD kAvx2Feature = 40; // PF_AVX2_INSTRUCTIONS_AVAILABLE（古い SDK には定義がない）
    CCL_LOG_ALWAYS(LogCategory::Core, "[環境] CPU: %s / 論理コア %u / AVX2: %s",
                   brand, std::thread::hardware_concurrency(), IsProcessorFeaturePresent(kAvx2Feature) ? "あり" : "なし");

    // --- メモリ ---
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    CCL_LOG_ALWAYS(LogCategory::Core, "[環境] メモリ: %llu MB（空き %llu MB）",
                   memory.ullTotalPhys / (1024 * 1024), memory.ullAvailPhys / (1024 * 1024));

    // --- 起動したフォルダ ---
    // ファイルはすべてここからの相対パスで読む。Assets と Data.pak のどちらが隣にあるかも出す
    std::error_code ec;
    const auto      cwd = std::filesystem::current_path(ec);
    CCL_LOG_ALWAYS(LogCategory::Core, "[環境] フォルダ: %s / Assets: %s / Data.pak: %s",
                   cwd.u8string().c_str(),
                   std::filesystem::is_directory(cwd / "Assets", ec) ? "あり" : "なし",
                   std::filesystem::is_regular_file(cwd / "Data.pak", ec) ? "あり" : "なし");
}



int Framework::Run()
{
    // 処理フロー（メッセージがないときの1フレーム）:
    // 1. 垂直同期を待ち、経過時間を測る
    // 2. ゲーム時間と、ロジックを止めるかを決める
    // 3. 終了要求を消化する
    // 4. 時間を貯め、計測表示を更新する
    // 5. 入力を取得する
    // 6. エディタのロード・セーブ要求を消化する
    // 7. 貯めた時間の分だけ固定ステップを進める
    // 8. 描画補間の割合（alpha）を World へ渡す
    // 9. 可変フレームの更新と描画

    // 一時停止の扱いは 2・4・8 に分かれている。2 で止めるかを決め、4 で時間を貯めず、8 で alpha を 1 に固定する
    // どれか1つでも欠けると、止まっているはずの絵が prev と現在の間を往復して震える

    MSG msg = {};

    // まだ FixedUpdate に消化していない経過時間（秒）
    double accumulator = 0.0;

    timer.Tick();

    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            // ---------------------------------------------------------
            // 1. 垂直同期待ちと経過時間
            // ---------------------------------------------------------

            // 垂直同期待ちの集約点。前フレームの提示が終わるまでここでブロックする
            // Waitable スワップチェーン（FRAME_LATENCY_WAITABLE_OBJECT）を使うと、フリップモデルで
            // 各パス（特にフレーム先頭の ShadowPass）へ紛れ込んでいた垂直同期待ちが、この1本のゾーンに集まる

            // その結果、Tracy 上で ShadowPass などが本来のコストで読めるようになり、入力遅延も減る
            {
                ZoneScopedN("Wait: Frame Latency (VSync)");
                Graphics::Instance().WaitForFrameLatency();
            }

            timer.Tick();

            float frameTime = timer.TimeInterval();

            // TODO(2026-09-16): シャドウマップ周りでメモリリークの疑い
            //   リークダンプの確保番号を _CrtSetBreakAlloc に渡して確保箇所を特定する
            //   （実行ごとに番号が変わる場合はこの方法は使えない）

            // どんなに重くても 0.1 秒進んだことにして打ち切る
            // 打ち切らないと、処理落ちで消化するステップが増え、さらに処理落ちする、を繰り返して止まる
            if (frameTime > 0.1f) frameTime = 0.1f;

            // ---------------------------------------------------------
            // 2. ゲーム時間と停止の判定
            // ---------------------------------------------------------

            // ゲームに渡す経過時間。エディタのデバッグ速度を掛けて、世界全体をスロー再生する
            // 時間の出どころで掛けるので、ロジック・カメラ・ムービーなどすべての時計が同じ比率で遅くなる

            // FIXED_DT は変えずにステップの回数だけが減るため、移動距離やフェーズの長さは等速と同じ

            // 入力（振動の残り時間）と FPS 表示は実時間（frameTime）のまま進める
            float gameTime = frameTime;
#ifdef ENABLE_EDITOR
            gameTime *= m_editorContext.debugSpeed;
#endif

            // 一時停止中はゲーム内時間（accumulator）を進めない（4 と 8 で使う）
            // 進めると、FixedUpdate が止まって prevPosition / position が固定されているのに alpha だけが
            // 実時間で 0〜1 を往復し、TransformInterpolationSystem が両者を往復補間してキャラが震える
            bool logicFrozen  = false;
            bool doSingleStep = false;
#ifdef ENABLE_EDITOR
            logicFrozen = m_editorContext.isPaused;

            // コマ送り。一時停止中に要求されたら、このフレームだけ1物理ステップ進める
            if (logicFrozen && m_editorContext.stepOneFrame)
            {
                doSingleStep = true;
            }
            m_editorContext.stepOneFrame = false; // 要求は毎フレーム消費する（連続送りはキー側のエッジ検出で防ぐ）
#endif

            // ゲームのポーズ（PauseSystem）でも世界の時計を止める
            // エディタに依存せず Shipping でも効くよう、Engine 層の TimeContext.isPaused を見て logicFrozen に合流させる
            // 合流させないと、エディタの一時停止と同じ理由でポーズ中のキャラやパーティクルが震える
            if (auto *pausedScene = SceneManager::Instance().GetCurrentScene())
                if (auto *pausedWorld = pausedScene->GetWorld())
                    if (pausedWorld->HasResource<TimeContext>() &&
                        pausedWorld->GetResource<TimeContext>().isPaused)
                        logicFrozen = true;

            // ---------------------------------------------------------
            // 3. 終了要求
            // ---------------------------------------------------------

            // タイトル画面の QUIT 要求（GameFlowState.quitRequested）を、メインスレッドのここで消化する
            // System の Update はジョブスケジューラ経由でワーカースレッドで走りうる
            // System から PostQuitMessage を直接呼ぶと WM_QUIT が別スレッドのキューに積まれ、
            // このメインループの PeekMessage が拾えずに終了しない
            if (auto *scene = SceneManager::Instance().GetCurrentScene())
                if (auto *world = scene->GetWorld())
                    if (world->HasResource<GameFlowState>() &&
                        world->GetResource<GameFlowState>().quitRequested)
                    {
                        PostQuitMessage(0);
                    }

            // ---------------------------------------------------------
            // 4. 時間の蓄積と計測表示
            // ---------------------------------------------------------

            // 停止中は貯めない
            if (!logicFrozen)
            {
                accumulator += gameTime;
            }

            // 1秒ごとに、ウィンドウタイトルへ FPS と FixedUpdate 回数を出す（実時間で数える）
            m_debugTimer += frameTime;
            if (m_debugTimer >= 1.0f)
            {
#ifdef ENABLE_EDITOR
                std::string title = "FPS: " + std::to_string(ImGui::GetIO().Framerate) +
                                    " | Fixed Updates: " + std::to_string(m_fixedUpdateCounter);
                SetWindowTextA(hWnd, title.c_str());
#endif

                m_fixedUpdateCounter = 0;
                m_debugTimer         = 0.0f;
            }

            // ---------------------------------------------------------
            // 5. 入力
            // ---------------------------------------------------------

            // FixedUpdate のループに入る前に、必ず最新のハードウェア入力を取得する
            {
                ZoneScopedN("Framework::Input::Update");
                Input::Instance().Update(frameTime);
            }

            // ---------------------------------------------------------
            // 6. エディタのロード・セーブ要求
            // ---------------------------------------------------------

            // エディタからのロード・セーブ要求を、フレーム先頭の一点で消化する
            // まだ誰も World の処理を始めていないこの時点で入れ替えれば、
            // 以降の FixedUpdate / Update / Render は一貫して新しい World を処理する
#ifdef ENABLE_EDITOR
            if (auto *currentScene = SceneManager::Instance().GetCurrentScene())
            {
                auto *world = currentScene->GetWorld();
                if (world)
                {
                    // シーンのロード要求
                    if (!m_editorContext.pendingLoadScenePath.empty())
                    {
                        ZoneScopedN("Editor::ExecutePendingLoad");

                        // シーンマネージャーを直接叩いて、この世界のエンティティを入れ替える
                        SceneManager::Instance().LoadScene(world, m_editorContext.pendingLoadScenePath);

                        m_editorContext.pendingLoadScenePath.clear();
                        m_editorContext.ClearSelection();
                    }

                    // シーンのセーブ要求
                    if (!m_editorContext.pendingSaveScenePath.empty())
                    {
                        ZoneScopedN("Editor::ExecutePendingSave");

                        SceneManager::Instance().SaveScene(world, m_editorContext.pendingSaveScenePath);
                        m_editorContext.pendingSaveScenePath.clear();
                    }

                    // レベル(ステージ+プレハブ参照)のロード要求
                    if (!m_editorContext.pendingLoadLevelPath.empty())
                    {
                        ZoneScopedN("Editor::ExecutePendingLoadLevel");

                        LevelSerializer::LoadLevel(world, m_editorContext.pendingLoadLevelPath);
                        m_editorContext.pendingLoadLevelPath.clear();
                        m_editorContext.ClearSelection();
                    }

                    // レベルのセーブ要求
                    if (!m_editorContext.pendingSaveLevelPath.empty())
                    {
                        ZoneScopedN("Editor::ExecutePendingSaveLevel");

                        LevelSerializer::SaveLevel(world, m_editorContext.pendingSaveLevelPath);
                        m_editorContext.pendingSaveLevelPath.clear();
                    }
                }
            }
#endif

            // ---------------------------------------------------------
            // 7. 固定ステップ（Simulation）
            // ---------------------------------------------------------

            int       updateCount = 0;
            const int MAX_STEPS   = 4; // 1フレームに許容する FixedUpdate の最大回数

            // Tracy Section で1つの帯にする。プロファイラ側で「この工程だけ」に時間範囲を絞れるので、
            // ForEachParallel のフォークジョイン（join 待ちのカーネル時間）をこの帯の中だけで追跡できる
            {
                TRACY_SECTION("Simulation");

                // コマ送り。accumulator に関係なく、確実に1物理ステップだけ進める
                // 一時停止中は accumulator が溜まらないので、下の通常のループは回らない
                if (doSingleStep)
                {
                    FixedUpdate(FIXED_DT);
                    m_fixedUpdateCounter++;
                }

                while (accumulator >= FIXED_DT && updateCount < MAX_STEPS)
                {
                    FixedUpdate(FIXED_DT);

                    accumulator -= FIXED_DT;
                    updateCount++;

                    m_fixedUpdateCounter++;
                }

                // 最大回数を超えてまだ時間が余っている場合は、無理に消化せず FIXED_DT 未満の余りだけ残して捨てる
                // 処理落ちしたときにゲーム速度がスローになるが、カクつきは防げる
                if (accumulator >= FIXED_DT)
                {
                    accumulator = fmod(accumulator, FIXED_DT);
                }
            }

            // ---------------------------------------------------------
            // 8. 描画補間の割合
            // ---------------------------------------------------------

            // accumulator / FIXED_DT は、次の固定ステップに対してどれだけ進んでいるか（0〜1）
            // TransformInterpolationSystem が、1つ前と最新のステップの間をこの割合で補間する
            if (auto *currentScene = SceneManager::Instance().GetCurrentScene())
            {
                if (auto *world = currentScene->GetWorld())
                {
                    // GetResource は未登録だと assert で落ちるので、必ず HasResource で確かめる
                    if (world->HasResource<RenderInterpState>())
                    {
                        // 一時停止中は補間せず、最新の確定姿勢（alpha = 1）を表示して震えを止める
                        world->GetResource<RenderInterpState>().alpha =
                            logicFrozen ? 1.0f : static_cast<float>(accumulator / FIXED_DT);
                    }
                }
            }

            // ---------------------------------------------------------
            // 9. 可変フレームの更新と描画
            // ---------------------------------------------------------

            // Update / Render もそれぞれ Section の帯にして、Tracy のタイムライン上で
            // Simulation と描画の工程を色分け・絞り込みできるようにする
            {
                TRACY_SECTION("Update");
                Update(gameTime);
            }
            {
                TRACY_SECTION("Render");
                Render(gameTime);
            }

            // 1フレームの終わりを Tracy に通知する
            FrameMark;
        }
    }
    return static_cast<int>(msg.wParam);
}



LRESULT CALLBACK Framework::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // OSからのメッセージ処理がスパイクの原因になることもあるため追跡
    ZoneScopedN("Framework::HandleMessage");

#ifdef ENABLE_EDITOR
    if (ImGuiRenderer::HandleMessage(hWnd, msg, wParam, lParam))
        return true;
#endif

    switch (msg)
    {
    case WM_MOUSEWHEEL: {
        // ホイール1ノッチ（120）を WHEEL_DELTA（120）で割って、+1 または -1 にする
        int scroll = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        Input::Instance().GetMouse().SetWheel(scroll);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         hdc;
        hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_CREATE:
        break;
    case WM_KEYDOWN:
        // キー入力はここでは扱わない（ESC のポーズも含めて InputSystem 側）

        // ESC などのキーでアプリを終了させない。ゲームとしての終了の出口は、タイトル画面の QUIT にまとめている
        break;
    case WM_ACTIVATEAPP:
        // アプリが前面から外れた・戻った。ゲーム側はこれを見て、キーの押しっぱなしを消し、自動で一時停止する
        Input::Instance().SetAppActive(wParam != FALSE);
        break;
    case WM_ENTERSIZEMOVE:
        // ウィンドウの移動・リサイズ操作の開始。操作中はタイマーを止め、
        // 解放後に大きな経過時間がまとめて流れ込むのを防ぐ
        timer.Stop();
        break;
    case WM_EXITSIZEMOVE:
        // 移動・リサイズ操作の終了。タイマーを再開する
        timer.Start();
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}
