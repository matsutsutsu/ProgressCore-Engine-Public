#include <memory>
#include <sstream>
#include <algorithm> // min/max用
#include <string>    // std::string, std::to_string用
#include <windowsx.h>

#include "Framework.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Input/Input.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Time/Components/TimeState.h"         // TimeContext.isPaused（世界時計の凍結。accumulator/補間を止める）
#include "Engine/Core/App/GameFlowState.h" // GameFlowState.quitRequested（QUITをメインスレッドで消化する）
#include "Engine/Serialization/Serializer/LevelSerializer.h"
#include "Engine/Scene/BaseScene.h"

// APIファイルをインクルード
#include "Engine/Audio/DirectXTKIntegration/DxtkAudioManager.h" // オーディオは DXTK Audio を使用
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Platform/Input/InputFacade.h"

#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/LogSinks.h"

#include "EngineServices.h"
#include "Engine/ECS/Jobs/CCL_JobSystem.h" // 共有ジョブプールの実体生成に必要
#include "Engine/Core/Components/RenderInterpState.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Core/EditorWindowManager.h"
#include "Editor/Core/EditorCommandHistory.h" // シーン切替時の履歴破棄
#include "Editor/Core/EditorContext.h"
#include "Editor/Windows/General/ConsoleWindow.h" // ConsoleWindow は編集時のみ使用（GetWindow<ConsoleWindow>）
#include "Engine/Graphics/Renderer/ImGuiRenderer.h"
#include "Engine/Graphics/Renderer/Core/RenderDebugRegistry.h"
#endif

#include "tracy/Tracy.hpp"
#include "tracy/TracyD3D11.hpp"
#include "Engine/Core/Profiling/TracySection.h" // フレーム内フェーズ(Simulation/Render)の帯

// Present の垂直同期間隔（1 = 毎 VSync で表示）
static const int syncInterval = 1;

Framework::Framework(HWND hWnd)
    : hWnd(hWnd)
{
    ZoneScopedN("Framework::Constructor");

    // 最初に Logger をセットアップする（他システムが起動時に CCL_LOG を呼ぶため）
    {
        ZoneScopedN("Init: Logger");
        Logger::Instance().AddSink(std::make_shared<DebugStreamSink>());
        // ログファイルの出力先。開発時は Build/ 配下に集約、製品(Shipping)は exe 隣の Logs/ に。
        // FileSink は遅延生成なので、Shipping では ERROR が実際に出た時だけファイルが作られる
        //（正常終了なら何も残らない）。起動ごとに前回分を Engine.prev.log へ退避する2世代式。
#ifdef ENABLE_EDITOR
        const char *kLogPath = "Build/Logs/Engine.log";
#else
        const char *kLogPath = "Logs/Engine.log";
#endif
        Logger::Instance().AddSink(std::make_shared<FileSink>(kLogPath));
        m_consoleLogSink = std::make_shared<ImGuiBufferSink>(2000);
        Logger::Instance().AddSink(m_consoleLogSink);
    }

    {
        ZoneScopedN("Init: Graphics");
        Graphics::Instance().Initialize(hWnd);
    }
#ifdef ENABLE_EDITOR
    {
        ZoneScopedN("Init: ImGuiRenderer");
        ImGuiRenderer::Initialize(hWnd, Graphics::Instance().GetDevice(), Graphics::Instance().GetDeviceContext());
    }
#endif
    // FileDialog を一括初期化する（エディタ専用）。
    //   プリセット登録 + 前回の履歴ロード（Build/User/EditorPrefs.json）がここで完結する。
    //   ネイティブのファイル選択ダイアログはエディタ機能なので、Shipping では初期化しない
    //   （＝EditorPrefs.json を生成しない）。
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
        // ECS のフレーム並列で全 World が共有する唯一のジョブプールを、ここ（上層）で1つだけ生成する。
        // 以降 SystemManager はこれを注入して使う（各 World でプールを持たない）
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

        // 共有スレッドプールを渡すと、物理ジョブも ECS と同じプールで走る
        // （JoltJobSystemAdapter 経由。プールが1本になり、物理ジョブが
        //   Tracy にジョブ名付きゾーンとして表示されるようになる）。
        // Jolt 純正のプールを使いたい場合は、引数を外して
        // Initialize() と呼ぶだけでよい（比較計測用に両方残してある）。
        services.physicsManager->Initialize(services.jobSystem.get());
    }

    // この中で最初のシーン生成予約まで行う
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
        // 描画パイプラインを最後に破棄する（ResourceManager のクリアもここで行われる）。
        // デバッグビルドでは、ここで解放漏れの GPU オブジェクトが ReportLiveDeviceObjects で出力される
        Graphics::Instance().Shutdown();
    }
}

void Framework::FixedUpdate(float fixedTime)
{
    // 物理・ロジックなど、フレームレートで結果が変わってはいけない処理だけを進める

    ZoneScopedN("Framework::FixedUpdate");

    // 一時停止/コマ送りの判断は Run() 側で一元管理する。
    //   ここが呼ばれた時点で「1物理ステップ進めてよい」ことは確定しているため、
    //   本関数は無条件にロジックを進める（停止中は Run() が本関数を呼ばない）。
    SceneManager::Instance().FixedUpdate(fixedTime);

    // このステップで消費した「押した瞬間」系の入力フラグを下ろす
    Input::Instance().ClearLogicInput();
}

void Framework::Update(float elapsedTime)
{
    // 描画準備・UI など、毎フレーム行いたい処理

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

        // シーンが切り替わったら、前のシーンを指していたエディタ側の参照を全部捨てる。
        //
        //   Undo 履歴は ChangeComponentCommand が World* を生ポインタで、対象を
        //   EntityID で持つ。シーン切替は SceneManager::Clear() が currentScene ごと
        //   delete するので、残したまま Ctrl+Z を押すと解放済みメモリを書きに行く。
        //   EntityID の世代も World ごとに 1 から振り直されるため、番地が生きていても
        //   別のエンティティに命中する。選択(selectedEntity)も同じ理由で無効になる。
        //
        //   SceneManager は Engine 層なので Editor を include できない。切替の検知だけ
        //   通し番号で受け取り、破棄はエディタ側を知っているここで行う。
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
    // Tracy の GPU タイムスタンプ回収はフレーム先頭で行う。
    // 前フレームの Present / Flush から最も時間が経っており、
    // GPU 側の書き込みが完了している可能性が最も高いため。
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

                // デバッグ表示（Render Debug ウィンドウ）へ device レベルの SRV を登録する。
                // パス出力ではないため、SRV が確定するこの位置（描画完了後）で申告する。
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
            // context.renderExternalWindows を false にして止められる。
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
            // 失敗時（デバイスロスト等）は Flush や Collect を呼んではいけない。
            // GPU リソースを作り直し、シーンにも作り直しを知らせる
            Graphics::Instance().RecoverFromDeviceLost();
            if (auto *scene = dynamic_cast<BaseScene *>(SceneManager::Instance().GetCurrentScene()))
                scene->OnDeviceRecreated();
        }
    }
}

int Framework::Run()
{
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
            // vsync待ちの集約点：前フレームの提示が終わるまでここでブロックする。
            //   Waitable スワップチェーン(FRAME_LATENCY_WAITABLE_OBJECT)のおかげで、
            //   フリップモデルで各パス(特にフレーム先頭の ShadowPass)へ紛れ込んでいた
            //   「垂直同期待ち」が、この1本の明示ゾーンに集約される。
            //   → Tracy 上で ShadowPass 等が本来の実コストで読めるようになる＋入力遅延も減る。
            {
                ZoneScopedN("Wait: Frame Latency (VSync)");
                Graphics::Instance().WaitForFrameLatency();
            }

            timer.Tick();

            // 1. 前フレームからの経過時間を取得する
            float frameTime = timer.TimeInterval();

            // TODO(2026-09-16): シャドウマップ周りでメモリリークの疑い。
            //   リークダンプの確保番号を _CrtSetBreakAlloc に渡して確保箇所を特定する
            //   （実行ごとに番号が変わる場合はこの方法は使えない）

            // 2. 「死の螺旋」防止（Safety Cap）。
            //    どんなに重くても 0.1秒 進んだことにして打ち切る。
            //    これがないと、処理落ち→消化すべきステップが増える→さらに処理落ち… で止まる
            if (frameTime > 0.1f) frameTime = 0.1f;

            // 一時停止中はゲーム内時間(accumulator)を進めない。
            //   進めてしまうと FixedUpdate(ロジック)が止まって prevPosition/position が
            //   固定されているのに alpha だけ実時間で 0↔1 に揺れ続け、
            //   TransformInterpolationSystem が両者を往復補間してキャラが「震える」。
            bool logicFrozen  = false;
            bool doSingleStep = false;
#ifdef ENABLE_EDITOR
            logicFrozen = m_editorContext.isPaused;
            // コマ送り：一時停止中に要求されたら、このフレームだけ1物理ステップ進める
            if (logicFrozen && m_editorContext.stepOneFrame)
            {
                doSingleStep = true;
            }
            m_editorContext.stepOneFrame = false; // 要求は毎フレーム消費（連続送りはキー側のedge検出で防ぐ）
#endif

            // ゲームのポーズ（PauseSystem）でも世界の時計は止まる。エディタ非依存・Shipping 対応の
            //   ため、Engine 層の TimeContext.isPaused を見て logicFrozen に合流させる。これを入れないと
            //   accumulator だけ実時間で溜まり、alpha が 0↔1 に揺れて prev/current の固定姿勢を往復補間し、
            //   ポーズ中なのにキャラ/パーティクルが「震える」。（上の解説コメント参照）
            if (auto *pausedScene = SceneManager::Instance().GetCurrentScene())
                if (auto *pausedWorld = pausedScene->GetWorld())
                    if (pausedWorld->HasResource<TimeContext>() &&
                        pausedWorld->GetResource<TimeContext>().isPaused)
                        logicFrozen = true;

            // タイトル画面の QUIT 要求をメインスレッドで消化する。
            //   システムの Update はジョブスケジューラ経由でワーカースレッドで走りうるため、
            //   PostQuitMessage を各システムから直接呼ぶと WM_QUIT が別スレッドのキューに積まれ、
            //   このメインループ(PeekMessage)が拾えず終了しない。フラグ経由で必ずここ(main)で呼ぶ。
            if (auto *scene = SceneManager::Instance().GetCurrentScene())
                if (auto *world = scene->GetWorld())
                    if (world->HasResource<GameFlowState>() &&
                        world->GetResource<GameFlowState>().quitRequested)
                    {
                        PostQuitMessage(0);
                    }

            // 3. 時間を貯める（停止中は貯めない）
            if (!logicFrozen)
            {
                accumulator += frameTime;
            }

            // 1秒ごとに、ウィンドウタイトルへ FPS と FixedUpdate 回数を出す
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

            // FixedUpdate のループに入る前に、必ず最新のハードウェア入力を取得する
            {
                ZoneScopedN("Framework::Input::Update");
                Input::Instance().Update(frameTime);
            }

            // エディタからのロード/セーブ要求を、フレーム先頭の一点で消化する。
            //   誰も世界の処理を始めていないこの瞬間に対処することで、
            //   以降の FixedUpdate, Update, Render は一貫して「新しい世界」を走る。
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

            int       updateCount = 0;
            const int MAX_STEPS   = 4; // 1フレームに許容する FixedUpdate の最大回数

            // 3. Simulation フェーズ（固定タイムステップの論理・物理）。
            //   Tracy Section で1つの帯にする。プロファイラ側で「この工程だけ」に時間範囲を
            //   絞れるので、ForEachParallel のフォークジョイン（join待ちのカーネル時間）を
            //   この帯の中だけで追跡でき、固定タイムステップ構造の説明にも使える。
            {
                TRACY_SECTION("Simulation");

                // コマ送り：accumulator に関係なく、確実に1物理ステップだけ進める。
                //   一時停止中なので下の通常ループ(while)は accumulator が溜まらず回らない。
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

                // 最大回数を超えてまだ時間が余っている場合は、無理に消化せず
                // FIXED_DT 未満の余りだけ残して捨てる
                // （処理落ち時にゲーム速度がスローになるが、カクつきは防げる）
                if (accumulator >= FIXED_DT)
                {
                    accumulator = fmod(accumulator, FIXED_DT);
                }
            }

            // 4. 描画補間係数を算出して World へ渡す。
            //   accumulator / FIXED_DT = 次の物理ステップに対してどれだけ進んでいるか（0..1）
            if (auto *currentScene = SceneManager::Instance().GetCurrentScene())
            {
                if (auto *world = currentScene->GetWorld())
                {
                    // GetResource は未登録だと assert で落ちるので、必ず HasResource で確かめる
                    if (world->HasResource<RenderInterpState>())
                    {
                        // 一時停止中は補間せず最新の確定姿勢(alpha=1)を表示して震えを止める
                        world->GetResource<RenderInterpState>().alpha =
                            logicFrozen ? 1.0f : static_cast<float>(accumulator / FIXED_DT);
                    }
                }
            }

            // 5. 可変フレームの更新と描画。
            //   Update / Render もそれぞれ Section の帯にして、タイムライン上で
            //   Simulation と描画工程を色分け・絞り込みできるようにする。
            {
                TRACY_SECTION("Update");
                Update(frameTime);
            }
            {
                TRACY_SECTION("Render");
                Render(frameTime);
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
        // キー入力はここでは扱わない（ESC のポーズも含めて InputSystem 側）。
        // アプリ終了はウィンドウの×ボタン（WM_CLOSE→WM_DESTROY）だけ。
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
