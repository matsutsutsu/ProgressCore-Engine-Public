#ifdef ENABLE_EDITOR
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <ImGuizmo.h>
#include "ImGuiRenderer.h"
#include <tracy/Tracy.hpp>
#include"Engine/UI/ImGui/ImGuiThemes.h"
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 静的メンバ変数の実体定義
ImFont *ImGuiRenderer::s_editorFont = nullptr;
bool ImGuiRenderer::s_platformWindowsEnabled = true;

// 初期化
void ImGuiRenderer::Initialize(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* dc)
{
    ZoneScopedN("ImGuiRenderer::Initialize"); 

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	// フォントの読み込み
    ImFontConfig config;
    config.MergeMode           = false;
    const ImWchar *glyphRanges = io.Fonts->GetGlyphRangesJapanese();

	// 1つ目: UI全体で使うデフォルトフォント (Windows標準のメイリオ等)
    io.Fonts->AddFontFromFileTTF("Assets/Fonts/ArialUni.ttf", 18.0f, &config, glyphRanges);
    //io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", 16.0f, &config, glyphRanges);

	// 2つ目: エディタ専用の等幅フォント (Ricty等) を読み込んで、ポインタを保持する
    //s_editorFont =  io.Fonts->AddFontFromFileTTF("Assets/Fonts/ArialUni.ttf", 20.0f, &config, glyphRanges);	


	io.IniFilename = "Build/Logs/imgui.ini"; // 設定ファイルの名前を変更 (同じフォルダに保存される)
	io.LogFilename = "Build/Logs/imgui_log.txt"; // ログファイルの名前を変更 (同じフォルダに保存される)


	// =========================================================================
	// [1] コア機能の有効化 (Core Features)
	// =========================================================================

	// 【ナビゲーション】
	// キーボードによるUI操作（Tabキーでのフォーカス移動、Enterでの決定など）を有効化。
	// エディタ作業の生産性（マウスとキーボードの往復を減らす）のために通常は有効化します。
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; 

	// ゲームパッド（XInput等）でのUI操作を有効化。
	// エディタではなく、ゲーム本編のデバッグUIやプレイヤー向けUIを構築する際に使用します。
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// 【ドッキング】
	// ウィンドウ同士を結合（Docking）したり、画面分割するための機能を有効化。
	// Scene / Game / Inspector を並べるエディタ用レイアウトに必要。
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// 【マルチビューポート】
	// ImGuiのウィンドウを、メインのDirectXウィンドウ（OSのウィンドウ）の外へドラッグし、
	// 独立した別のOSウィンドウとして切り離せるようにします。デュアルモニター開発環境で必須です。
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;


	// =========================================================================
	// [2] マルチビューポートの詳細挙動 (Viewport Behaviors)
	// ※ io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; が有効な場合のみ機能
	// =========================================================================

	// 外に切り離した独立ウィンドウ同士が重なった際、自動的に1つのウィンドウに結合（マージ）させない。
	// 意図せずウィンドウがくっつく煩わしさを防ぎたい場合に有効化します。
	//io.ConfigViewportsNoAutoMerge = true;

	// 外に切り離した独立ウィンドウを、Windowsのタスクバーに個別のアイコンとして表示しない。
	// ツールバーがUIウィンドウで埋め尽くされるのを防ぐため、通常は有効化（true）を推奨します。
	//io.ConfigViewportsNoTaskBarIcon = true;

	// 独立ウィンドウをメインウィンドウの「子」として扱わない。
	// 有効にすると、メインウィンドウを最小化しても独立したUIウィンドウは画面に残るようになります。
	//io.ConfigViewportsNoDefaultParent = true;


	// =========================================================================
	// [3] ドッキングの詳細挙動と操作性 (Docking & Interaction)
	// =========================================================================

	// 1つのウィンドウ単体で存在している場合でも、常に上部にタブバー（見出し）を表示する。
	// Webブラウザのような視覚的ルールに統一し、どこを掴めばドッキングできるかを明示します。
	//io.ConfigDockingAlwaysTabBar = true;

	// ウィンドウをドラッグして別の場所にドッキングさせようとしている最中、
	// 掴んでいるウィンドウ（Payload）を半透明にして、ドロップ先のUIを隠さないようにします。
	io.ConfigDockingTransparentPayload = true;

	// ImGui::DragFloat などの数値スライダーにおいて、1クリックするだけで即座に直接テキスト入力モードに移行する。
	// （デフォルトは「Ctrl+クリック」または「ダブルクリック」が必要）。
	// レベルデザイナー等の数値入力の手間を半減させる、強力な作業効率化フラグです。
	//io.ConfigDragClickToInputText = true;


	// =========================================================================
	// [4] スタイルと視覚的調整 (Style & Visuals)
	// =========================================================================
	ImGuiStyle& style = ImGui::GetStyle();

	// ウィンドウ左上にデフォルトで存在する「▼（折りたたみ/Collapse）」ボタンを完全に非表示にする。
	// 誤操作によってウィンドウがタイトルバーだけになり、エディタのレイアウトが崩れたように見える事故を防ぎます。
	style.WindowMenuButtonPosition = ImGuiDir_None;

	// ウィンドウをドッキングした際の、境界線（区切り・セパレータ）の当たり判定と描画の太さを設定。
	// ダークテーマ適用時など、境界線が見づらくサイズ調整のドラッグがしにくい問題を解消します。
	style.SeparatorTextBorderSize = 2.0f; // ※必要に応じて値を調整

#if 0
	io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP!
	io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI
#endif

	// Setup Dear ImGui style

	//ImGuiThemes::ApplyDark();
	ImGuiThemes::ApplyDarcula();
	//ImGuiThemes::ApplySkeltonDarcula();
	//ImGuiThemes::ApplyLight();
	//ImGuiThemes::ApplyUnityDark();
	//ImGuiThemes::ApplyVisualStudioDark();

	//ImGuiThemes::ApplyEditorDark();
	//ImGuiThemes::ApplyCatppuccinMocha();
	//ImGuiThemes::ApplyTokyoNight();
	//ImGuiThemes::ApplyDracula();
	//ImGuiThemes::ApplyNord();

	// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hWnd);
	ImGui_ImplDX11_Init(device, dc);

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("Assets/Fonts/ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//ImFont* font = io.Fonts->AddFontFromFileTTF("Assets/Fonts/RictyDiminished-Regular.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != NULL);
}

// 終了化
void ImGuiRenderer::Finalize()
{
    ZoneScopedN("ImGuiRenderer::Finalize");

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

// フレーム開始処理
void ImGuiRenderer::NewFrame()
{    
    ZoneScopedN("ImGuiRenderer::NewFrame");

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

	ImVec2 pos = ImGui::GetMainViewport()->WorkPos;
	ImVec2 size = ImGui::GetIO().DisplaySize;
	ImGuizmo::BeginFrame();
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);

#if 1
	// Docking
	const ImGuiWindowFlags window_flags = ImGuiWindowFlags_None
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus
		| ImGuiWindowFlags_NoBackground
		;
	const ImGuiDockNodeFlags docspace_flags = ImGuiDockNodeFlags_None
		//| ImGuiDockNodeFlags_KeepAliveOnly
		| ImGuiDockNodeFlags_PassthruCentralNode
		;

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);


	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	bool dock_open = true;
	if (ImGui::Begin("MainDockspace", &dock_open, window_flags))
	{
		ImGui::PopStyleVar(3);

		ImGuiID dockspaceId = ImGui::GetID("MyDockspace");
		ImGui::DockSpace(dockspaceId, ImVec2(0, 0), docspace_flags);
	}
	ImGui::End();
#endif
}

// 描画実行
void ImGuiRenderer::Render(ID3D11DeviceContext* context)
{
    ZoneScopedN("ImGuiRenderer::Render (Total)"); 

	{
        // 1. CPU側でUIの頂点データなどを計算・確定させる処理
        ZoneScopedN("ImGui::Render (Build Draw Data)");
        ImGui::Render();
    }

    {
        // 2. 確定した頂点データをDX11経由でGPUに流し込む処理
        ZoneScopedN("ImGui_ImplDX11_RenderDrawData (GPU Submit)");
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    // 3. Viewports機能（ウィンドウをエンジンの外に引っ張り出した場合）の処理
    ImGuiIO &io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        {
            ZoneScopedN("ImGui::UpdatePlatformWindows");
            ImGui::UpdatePlatformWindows();
        }
        if (s_platformWindowsEnabled)
        {
            ZoneScopedN("ImGui::RenderPlatformWindowsDefault");
            ImGui::RenderPlatformWindowsDefault();
        }
    }
}

void ImGuiRenderer::SetPlatformWindowsEnabled(bool enabled)
{
    s_platformWindowsEnabled = enabled;
}

// WIN32メッセージハンドラー
LRESULT ImGuiRenderer::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
}
