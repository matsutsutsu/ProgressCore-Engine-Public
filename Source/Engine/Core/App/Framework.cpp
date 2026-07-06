#include <memory>
#include <sstream>
#include <algorithm> // min/max用
#include <string> // std::string, std::to_string用
#include <windowsx.h>

#include "Framework.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Input/Input.h"
#include "Engine/Scene/SceneManager.h"
#include "Game/Scenes/BaseScene.h"

// APIファイルをインクルード
#include "Engine/Audio/FmodIntegration/FmodAudioManager.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Platform/Input/InputFacade.h"

#include "Engine/Serialization/Factory/Prefab.h"                 
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/LogSinks.h"
#include "Editor/Windows/ConsoleWindow.h"

#include "EngineServices.h"
#include "Engine/Core/Components/RenderInterpState.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Core/EditorWindowManager.h"
#include "Editor/Core/EditorContext.h"
#include "Engine/Graphics/Renderer/ImGuiRenderer.h"
#endif

#include "tracy/Tracy.hpp"
#include "tracy/TracyD3D11.hpp"


void InitializeDebugLayer(ID3D11Device *device)
{
    ID3D11InfoQueue *infoQueue = nullptr;
    device->QueryInterface(__uuidof(ID3D11InfoQueue), (void **)&infoQueue);

    if (infoQueue) {
        // 警告が出たらブレークポイントで止まるように設定
        infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, TRUE);
        infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        infoQueue->Release();
    }
}

// 垂直同期間隔設定
static const int syncInterval = 1;

// コンストラクタ
Framework::Framework(HWND hWnd)
	: hWnd(hWnd)
{
 
	ZoneScopedN("Framework::Constructor");


	// ★ 最初にLoggerをセットアップ (他システムが起動時に CCL_LOG を呼ぶため)
	Logger::Instance().AddSink(std::make_shared<DebugStreamSink>());
	Logger::Instance().AddSink(std::make_shared<FileSink>(
		"Build/Logs/Engine.log", 10ull * 1024 * 1024, 3));
	m_consoleLogSink = std::make_shared<ImGuiBufferSink>(2000);
	Logger::Instance().AddSink(m_consoleLogSink);


	// グラフィックス初期化
	Graphics::Instance().Initialize(hWnd);
#ifdef ENABLE_EDITOR
	// IMGUI初期化
	ImGuiRenderer::Initialize(hWnd, Graphics::Instance().GetDevice(), Graphics::Instance().GetDeviceContext());
#endif
	// ★ FileDialog を一括初期化
	//    プリセット登録 + 前回の履歴ロードがここで完結する
	FileDialog::Initialize();

	// Input 初期化
	Input::Instance().Initialize(hWnd);


	// =========================================================
	//  システムを生成・初期化し、仲介役に預ける
	// =========================================================
	auto& services = EngineServices::Instance();

	services.inputFacade = std::make_shared<InputFacade>(&Input::Instance());
	services.inputFacade->LoadConfig("Assets/Config/InputConfig.json");

	services.audioManager = std::make_shared<FmodAudioManager>();
	services.audioManager->Initialize();
	services.audioManager->LoadConfig("Assets/Config/AudioConfig.json");

	services.physicsManager = std::make_shared<JoltPhysicsManager>();
	services.physicsManager->Initialize();


	// シーンマネージャー初期化
	SceneManager::Instance().Initialize();	

#ifdef ENABLE_EDITOR
	// エディタの初期化をフレームワーク層で行う
	m_editorWindowManager = std::make_unique<EditorWindowManager>();
	m_editorWindowManager->Initialize();
	// 2. 状態の復元 
	m_editorWindowManager->LoadLayout("Build/User/WindowLayout.json");

	// ConsoleWindow に Sink を渡す
	if (auto* console = m_editorWindowManager->GetWindow<ConsoleWindow>())
		console->SetBufferSink(m_consoleLogSink);
#endif

}

// デストラクタ
Framework::~Framework()
{
	ZoneScopedN("Framework::Destrucor");


#ifdef ENABLE_EDITOR
	// 終了時に必ず保存
	m_editorWindowManager->SaveLayout("Build/User/WindowLayout.json");
	// エディタの解放を明示的に行う（スマートポインタなので自動解放されるが順序の担保として）
	m_editorWindowManager.reset();
#endif

	// シーン終了化
    {
        ZoneScopedN("Finalize: SceneManager");
        SceneManager::Instance().Clear();
    }
#ifdef ENABLE_EDITOR

	{
        ZoneScopedN("Finalize: ImGuiRenderer");
        // IMGUI終了化
        ImGuiRenderer::Finalize();
    }

#endif
    {
        ZoneScopedN("Finalize:  SaveFileDialogHistory");
        // ★ ダイアログ履歴を保存
        FileDialog::SaveHistory();
    }

	{
        ZoneScopedN("Finalize: Logger");
        Logger::Instance().Flush();
        Logger::Instance().ClearSinks(); // shared_ptr 循環参照防止
    }

	{
        ZoneScopedN("Finalize: Graphics::Shutdown");
        // ④ 描画パイプラインの破棄 (ここで ResourceManager のクリアも行われる)
        // 最後に残ったGPUの親玉を破壊します。
        // ここでついに、あの「ReportLiveDeviceObjects」のレポートが出力されます！
        Graphics::Instance().Shutdown();
    }

}

// ---------------------------------------------------------
// 固定更新：物理演算などの「結果が変わってはいけない処理」
// ---------------------------------------------------------
void Framework::FixedUpdate(float fixedTime)
{
	// ここでは「物理・ロジック系」の更新のみを行う
 
    // 固定フレームごとの更新時間を計測
    ZoneScopedN("Framework::FixedUpdate");


	bool shouldUpdateLogic = true;

#ifdef ENABLE_EDITOR
	// エディタが有効な場合、Play状態の時だけロジックを回す
	if (m_editorContext.playState != EditorPlayState::Playing) {
		shouldUpdateLogic = false;
	}
#endif

	// ★ 関所：shouldUpdateLogic が true の時だけゲーム内時間が進む
	if (shouldUpdateLogic) {
		// SceneManager に FixedUpdate を追加して呼ぶのが理想
		// まだ実装していない場合は、一旦コメントアウトかエラー回避しておく
		SceneManager::Instance().FixedUpdate(fixedTime);
	}


	 // 使い終わった入力フラグをここでリセット
	 Input::Instance().ClearLogicInput();
}

// ---------------------------------------------------------
// 可変更新：描画準備、入力、UIなど「毎フレーム行いたい処理」
// ---------------------------------------------------------
void Framework::Update(float elapsedTime)
{
 
    // 毎フレームのロジック更新時間を計測
    ZoneScopedN("Framework::Update");


#ifdef ENABLE_EDITOR
	{
 
		ZoneScopedN("Framework::ImGuiRenderer::NewFrame");


		// IMGUIフレーム開始処理	
		ImGuiRenderer::NewFrame();
	}
#endif

	{
 
		ZoneScopedN("Framework::SceneManager::Update");


		// シーン更新処理
		SceneManager::Instance().Update(elapsedTime);
	}

#ifdef ENABLE_EDITOR
	{
 
		ZoneScopedN("Framework::EditorUpdate");


		// ★ 重要: 現在アクティブなシーンの情報をエディタに供給する（オブザーバー的アプローチ）
		// ※ SceneManager に GetCurrentScene() などのアクセス関数を追加する必要があります
		auto* currentScene = SceneManager::Instance().GetCurrentScene();
		if (currentScene) {
			m_editorContext.world = currentScene->GetWorld();
			m_editorContext.systemManager = currentScene->GetSystemManager();
		
		}
		else {
			m_editorContext.world = nullptr;
			m_editorContext.systemManager = nullptr;
		}

		// UIからのリクエスト（シーン遷移など）があれば、ここでFrameworkやSceneManagerに指示を出す
		if (!m_editorContext.pendingLoadScenePath.empty()) {
			// 例: SceneManager::Instance().LoadScene(m_editorContext.pendingLoadScenePath);
			m_editorContext.pendingLoadScenePath.clear();
		}
	}
#endif
}

// 描画処理
void Framework::Render(float elapsedTime)
{
	// =========================================================
	// ★ 究極の修正: 【一番最初】にTracyを回収する
	// 前フレームの Present と Flush から最も時間が経過しているこの瞬間が、
	// GPUのタイムスタンプ書き込みが完了している確率が最も高い安全地帯です。
	// =========================================================
	if (auto tracyCtx = Graphics::Instance().GetTracyContext()) {
		TracyD3D11Collect(tracyCtx);
	}

    // 描画コマンド発行全体を計測
    ZoneScopedN("Framework::Render");


	ID3D11DeviceContext* dc = Graphics::Instance().GetDeviceContext();

	// =========================================================
	// 　ビルド構成による描画先の切り替え
	// =========================================================
#ifdef ENABLE_EDITOR
	// 【エディタあり (Development)】: ゲームは裏画面(gameRTV)に描画する
	bool useGameRTV = true;
#else
	// 【エディタなし (Shipping)】: ゲームは直接実際の画面(バックバッファ)に描画する
	bool useGameRTV = false;
#endif

	// 画面クリア
	Graphics::Instance().Clear(0.5f, 0.5f, 0.5f, 1, useGameRTV);
	// レンダーターゲット設定
	Graphics::Instance().SetRenderTargets(useGameRTV);


	// シーンマネージャーを介して現在のシーンを描画
    { 
        // シーンの描画（不透明、半透明、パーティクルなど）を細分化して計測
        ZoneScopedN("SceneManager::Render");


        SceneManager::Instance().Render();
    }



#ifdef ENABLE_EDITOR

	{
		ZoneScopedN("Framework::Clear SetRenderTargets");

		// =========================================================
		// 2. エディタ(UI)の描画（実際の画面＝バックバッファへ）
		// =========================================================
		// ★ false を渡して行き先をモニターに出力する実際の画面に戻す
		Graphics::Instance().Clear(0.1f, 0.1f, 0.1f, 1.0f, false);
		Graphics::Instance().SetRenderTargets(false);

	}

	// ImGuiの描画を計測
	{
	 
		ZoneScopedN("FrameWork::ImGui");


		{
		 
			ZoneScopedN("FrameWork::SceneManager::DrawGUI");

			SceneManager::Instance().DrawGUI();

		}

		{
		 
			ZoneScopedN("FrameWork::EditorUI::Render");


			// ゲームUIの後に、覆いかぶさるようにエディタUIを描画する
			if (m_editorWindowManager) {
				// ★ ここで完成したゲーム画面のテクスチャをUIに渡す！
				m_editorContext.gameSRV = (void*)Graphics::Instance().GetGameSRV();
				m_editorWindowManager->Draw(m_editorContext);

				if (m_editorWindowManager->IsVisible()) {
					m_editorContext.systemManager->OnGui();
				}
			}
		}

		{	 
			ZoneScopedN("FrameWork::ImGuiRenderer::Render");

			// GPU側のImGui処理時間も計測する
			if (auto tracyCtx = Graphics::Instance().GetTracyContext()) {
				TracyD3D11Zone(tracyCtx, "ImGui Pass (GPU)");
			}

			// IMGUI描画
			ImGuiRenderer::Render(dc);
		}

	}

#endif

	{
        // ★超重要: GPUの描画待ち（VSync同期）の時間を計測
        ZoneScopedN("Graphics::Present (VSync Wait)");
		
		// 画面表示
		if (Graphics::Instance().Present(syncInterval)) {
			// ★ 成功時のみ: GPUのキューを確実に押し出す
			if (dc) dc->Flush();
		}
		else {
			// ★ 失敗時(デバイスロスト等): 絶対に Flush や Collect を呼んではいけない
			// ① GPU リソース再構築
			Graphics::Instance().RecoverFromDeviceLost();
			// ② 現在のシーンに通知
			if (auto* scene = dynamic_cast<BaseScene*>(SceneManager::Instance().GetCurrentScene()))
				scene->OnDeviceRecreated();
		}
    }

	
}


// フレームレート計算
void Framework::CalculateFrameStats()
{
	// Code computes the average frames per second, and also the 
	// average time it takes to render one frame.  These stats 
	// are appended to the window caption bar.
	static int frames = 0;
	static float time_tlapsed = 0.0f;

	frames++;

	// Compute averages over one second period.
	if ((timer.TimeStamp() - time_tlapsed) >= 1.0f)
	{
		float fps = static_cast<float>(frames); // fps = frameCnt / 1
		float mspf = 1000.0f / fps;
		std::ostringstream outs;
		outs.precision(6);
		outs << "FPS : " << fps << " / " << "Frame Time : " << mspf << " (ms)";
		//SetWindowTextA(hWnd, outs.str().c_str());

		// Reset for next average.
		frames = 0;
		time_tlapsed += 1.0f;
	}
}

// ---------------------------------------------------------
// メインループ（Accumulator パターンの実装）
// ---------------------------------------------------------
int Framework::Run()
{
	MSG msg = {};

	// 時間の貯金箱
	double accumulator = 0.0;

	// 初回の時間計測
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
			timer.Tick();
			CalculateFrameStats();

			// 1. 経過時間を取得
			float frameTime = timer.TimeInterval();

			// メモリリーク検出を有効化 (すでにあるはず)
			_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

			// この番号のメモリ確保の瞬間にブレークする
			// ダンプに出ている番号（例: 2575）を指定します
			// ※実行するたびに番号が変わる場合は、この方法は使えません

			// TODO:シャドウマップでメモリリーク
			//_CrtSetBreakAlloc(121715);
			//_CrtSetBreakAlloc(121457);
			//_CrtSetBreakAlloc(121196);
			 
			//_CrtSetBreakAlloc(2275584);

			// 2. 「死の螺旋」防止（Safety Cap）
			// どんなに重くても、現実時間が 0.25秒 進んだことにして計算を打ち切る
			// これがないと、処理落ち→計算量増える→さらに処理落ち... で停止する
			if (frameTime > 0.1f) frameTime = 0.1f;

			// 3. 時間を貯める
			accumulator += frameTime;

			// 1秒ごとの計測処理
			m_debugTimer += frameTime;
			if (m_debugTimer >= 1.0f)
			{
#ifdef ENABLE_EDITOR
				// ウィンドウタイトルにFPSとFixedUpdate回数を表示
				std::string title = "FPS: " + std::to_string(ImGui::GetIO().Framerate) +
					" | Fixed Updates: " + std::to_string(m_fixedUpdateCounter);
				SetWindowTextA(hWnd, title.c_str());
#endif

				// カウンタをリセット
				m_fixedUpdateCounter = 0;
				m_debugTimer = 0.0f;
			}

			// =========================================================
			//  ループ(FixedUpdate)に入る前に、必ず最新のハードウェア入力を取得する
			// =========================================================
			{
				ZoneScopedN("Framework::Input::Update");
				Input::Instance().Update(frameTime);
			}

			// ===========================================================================
			// ★ あなたの提案による中央集権型シーン操作回路
			//   誰も世界の処理を始めていないこのクリーンな瞬間に対処することで、
			//   以降の FixedUpdate, Update, Render は一貫して「新しい世界」を走る。
			// ===========================================================================
#ifdef ENABLE_EDITOR

			if (auto* currentScene = SceneManager::Instance().GetCurrentScene())
			{
				auto* world = currentScene->GetWorld();
				if (world)
				{
					// ① ロード要求の一括検知
					if (!m_editorContext.pendingLoadScenePath.empty())
					{
						ZoneScopedN("Editor::ExecutePendingLoad");

						// シーンマネージャーを直接叩いて、この世界のエンティティを入れ替える
						SceneManager::Instance().LoadScene(world, m_editorContext.pendingLoadScenePath);

						// リクエストを完全に消化したのでクリア
						m_editorContext.pendingLoadScenePath.clear();
						m_editorContext.selectedEntity = CCL::ECS::InvalidEntityID;
					}

					// ② セーブ要求の一括検知
					if (!m_editorContext.pendingSaveScenePath.empty())
					{
						ZoneScopedN("Editor::ExecutePendingSave");

						SceneManager::Instance().SaveScene(world, m_editorContext.pendingSaveScenePath);
						m_editorContext.pendingSaveScenePath.clear();
					}
				}
			}
#endif
			// ===========================================================================


			// ループ回数制限用のカウンタ
			int updateCount = 0;
			const int MAX_STEPS = 4; // 1フレームに許容する最大更新回数


			while (accumulator >= FIXED_DT && updateCount < MAX_STEPS)
			{
				FixedUpdate(FIXED_DT);

				accumulator -= FIXED_DT;
				updateCount++;

				// カウントアップ
				m_fixedUpdateCounter++;
			}



			// もし最大回数を超えてまだ時間が余っている場合、
			// 無理に消化せず捨てる（処理落ち時にゲーム速度がスローになるが、カクつきは防げる）
			if (accumulator >= FIXED_DT)
			{
				// 残った時間を捨てる、あるいは次のフレームに持ち越すが
				// ここでは大きく溜まりすぎた分をカットしてリセットする例
				// accumulator = 0.0; // 完全に時間を捨てる場合

				// または、FIXED_DT未満になるように余りだけ残す（推奨）
				accumulator = fmod(accumulator, FIXED_DT);
			}

			
			// ===============================================================
            // ★ 描画補間係数の算出とWorldへの供給
            //   accumulator / FIXED_DT = 次の物理ステップに対してどれだけ「先読み」しているか
            // ===============================================================
            if (auto *currentScene = SceneManager::Instance().GetCurrentScene())
            {
                if (auto *world = currentScene->GetWorld())
                {
                    // ★ 規約準拠：必ず HasResource でガードしてから参照更新（O(1)・ヒープ確保なし）
                    if (world->HasResource<RenderInterpState>())
                    {
                        world->GetResource<RenderInterpState>().alpha =
                            static_cast<float>(accumulator / FIXED_DT);
                    }
                }
            }


			// 5. 残った時間 (accumulator) を使って「補間」を行うのが理想だが、
			//    まずは可変フレームで描画更新を行う
			Update(frameTime);
			Render(frameTime);

 
			// ここで1フレーム終了をTracyに通知
            FrameMark;


		}
	}
	return static_cast<int>(msg.wParam);
}

// メッセージハンドラ
LRESULT CALLBACK Framework::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // ★ OSからのメッセージ処理がスパイクの原因になることもあるため追跡
    ZoneScopedN("Framework::HandleMessage");

#ifdef ENABLE_EDITOR
	if (ImGuiRenderer::HandleMessage(hWnd, msg, wParam, lParam))
		return true;
#endif

	switch (msg)
	{
	case WM_MOUSEWHEEL:
	{
		// 1カリッ（120）を WHEEL_DELTA（120）で割って、+1 または -1 にする
		int scroll = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
		Input::Instance().GetMouse().SetWheel(scroll);
		return 0;
	}
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc;
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
		if (wParam == VK_ESCAPE) PostMessage(hWnd, WM_CLOSE, 0, 0);
		break;
	case WM_ENTERSIZEMOVE:
		// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
		timer.Stop();
		break;
	case WM_EXITSIZEMOVE:
		// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
		// Here we reset everything based on the new window dimensions.
		timer.Start();
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}
