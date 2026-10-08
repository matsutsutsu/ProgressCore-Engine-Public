#ifdef ENABLE_EDITOR
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <ImGuizmo.h>
#include "ImGuiRenderer.h"
#include <tracy/Tracy.hpp>
#include "Engine/UI/ImGui/ImGuiThemes.h"
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 操作をやめてから、間引かずに描き続ける時間（秒）。
// ホバーの色が戻る・ツールチップが消えるといった変化が数フレーム続くので、
// 操作が止まった瞬間に間引くと、途中の1コマが画面に残って見える
static constexpr float kPlatformActiveTail = 0.4f;

// 放置中に描き直す間隔（秒。約 15fps）。完全には止めない。FPS の表示などが止まって、
// 固まったように見えるのを避けるため。負荷と見た目の兼ね合いはここだけで調整する
static constexpr float kPlatformIdleInterval = 1.0f / 15.0f;

bool ImGuiRenderer::s_platformWindowsEnabled = true;

bool  ImGuiRenderer::s_platformWindowsThrottled = true;
float ImGuiRenderer::s_platformIdleTime         = 0.0f;
float ImGuiRenderer::s_platformIdleAccum        = 0.0f;
int   ImGuiRenderer::s_platformViewportCount    = 0;

// ---------------------------------------------------------------------------
// 初期化と終了
// ---------------------------------------------------------------------------

void ImGuiRenderer::Initialize(HWND hWnd, ID3D11Device *device, ID3D11DeviceContext *dc)
{
    ZoneScopedN("ImGuiRenderer::Initialize");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();

    // UI 全体のフォント。日本語を表示するので日本語のグリフを含める
    ImFontConfig config;
    config.MergeMode           = false;
    const ImWchar *glyphRanges = io.Fonts->GetGlyphRangesJapanese();
    io.Fonts->AddFontFromFileTTF("Assets/Fonts/ArialUni.ttf", 18.0f, &config, glyphRanges);

    io.IniFilename = "Build/Logs/imgui.ini";
    io.LogFilename = "Build/Logs/imgui_log.txt";

    // ウィンドウ同士のドッキング（Scene / Game / Inspector を並べるレイアウトに使う）
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // ImGui のウィンドウをメインウィンドウの外へ出し、別の OS ウィンドウにできるようにする
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // ドッキングのためにドラッグしている間、掴んでいるウィンドウを半透明にしてドロップ先を見せる
    io.ConfigDockingTransparentPayload = true;

    ImGuiStyle &style = ImGui::GetStyle();

    // ウィンドウ左上の折りたたみボタンを出さない。
    // 押し間違えてタイトルバーだけになり、レイアウトが崩れたように見えるのを防ぐ
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // SeparatorText の線の太さ（px）
    style.SeparatorTextBorderSize = 2.0f;

    ImGuiThemes::ApplyDarcula();

    // 別の OS ウィンドウにしたときも、メインウィンドウの中と同じ見た目にする
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(device, dc);

    // 切り離した OS ウィンドウのスワップチェーンについて（計測済み。変えないこと）:
    //   DX11 のバックエンドの既定は BufferCount = 1 / DXGI_SWAP_EFFECT_DISCARD（古い BLT モデル）。
    //   ImGui_ImplDX11_SetSwapChainDescs() でフリップモデル（FLIP_DISCARD / BufferCount = 2）に
    //   変えられる。実際に変えて、モニタ2枚・外に出したウィンドウ4枚・垂直同期なしで計った結果:
    //
    //     間引きあり + フリップ  3.97 / 3.91 ms      間引きあり + 既定  3.93 / 3.93 ms
    //
    //   差は出なかった。Present の回数を間引き（ShouldRenderPlatformWindows）で減らせば、
    //   1回あたりの方式の差は目立たない。一方で CreateSwapChain が失敗する環境では、
    //   IM_ASSERT は NDEBUG で消えるので、null のスワップチェーンを Present して落ちる。
    //   得が無くリスクだけが残るので変えていない。
}

void ImGuiRenderer::Finalize()
{
    ZoneScopedN("ImGuiRenderer::Finalize");

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

// ---------------------------------------------------------------------------
// 毎フレーム
// ---------------------------------------------------------------------------

void ImGuiRenderer::NewFrame()
{
    ZoneScopedN("ImGuiRenderer::NewFrame");

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();

    ImVec2 pos  = ImGui::GetMainViewport()->WorkPos;
    ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);

    // 画面全体を覆う枠なしのウィンドウを作り、その中をドックスペースにする。
    // PassthruCentralNode で、真ん中の何もドッキングしていない所はゲーム画面が透けて見え、操作も通る
    const ImGuiWindowFlags   window_flags   = ImGuiWindowFlags_None | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    const ImGuiDockNodeFlags docspace_flags = ImGuiDockNodeFlags_None
                                              | ImGuiDockNodeFlags_PassthruCentralNode;

    ImGuiViewport *viewport = ImGui::GetMainViewport();

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
}

void ImGuiRenderer::Render(ID3D11DeviceContext *context)
{
    ZoneScopedN("ImGuiRenderer::Render (Total)");

    {
        // CPU で UI の頂点データを作る
        ZoneScopedN("ImGui::Render (Build Draw Data)");
        ImGui::Render();
    }

    {
        // 作った頂点データを DX11 で描く
        ZoneScopedN("ImGui_ImplDX11_RenderDrawData (GPU Submit)");
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    // メインウィンドウの外に出したウィンドウ
    ImGuiIO &io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        {
            ZoneScopedN("ImGui::UpdatePlatformWindows");
            ImGui::UpdatePlatformWindows();
        }
        // UpdatePlatformWindows() は OS ウィンドウを作る・壊す・動かすので、毎フレーム呼ぶ。
        // 間引けるのは描画と Present だけ
        if (s_platformWindowsEnabled && ShouldRenderPlatformWindows())
        {
            ZoneScopedN("ImGui::RenderPlatformWindowsDefault");
            ImGui::RenderPlatformWindowsDefault();
        }
    }
}

// ---------------------------------------------------------------------------
// 別ウィンドウ（マルチビューポート）の描画制御
// ---------------------------------------------------------------------------

bool ImGuiRenderer::ShouldRenderPlatformWindows()
{
    // Unity（Repaint）や Unreal（Slate の Invalidation）のエディタは、変化したウィンドウだけを描き直す。
    // ImGui にはその仕組みが無いので、操作中は毎フレーム、放置中は低い頻度で描く、で近づける

    if (!s_platformWindowsThrottled) return true;

    ImGuiIO &io = ImGui::GetIO();

    // ウィンドウが増減したフレームは必ず描く。切り離した直後のウィンドウはまだ一度も
    // Present されていないので、飛ばすと次に描くまで中身が出ない
    const int viewportCount = ImGui::GetPlatformIO().Viewports.Size;
    if (viewportCount != s_platformViewportCount)
    {
        s_platformViewportCount = viewportCount;
        s_platformIdleTime      = 0.0f;
        s_platformIdleAccum     = 0.0f;
        return true;
    }

    // 操作中かどうかを、マウスの移動やボタンで直接判定してはいけない。プレイ中はマウスで
    // 視点を動かし続け、攻撃でボタンも押すので、一番間引きたい場面で常に描くことになる。
    // WantCaptureMouse はカーソルが ImGui のウィンドウの上にあるときだけ true で、
    // ゲーム画面（PassthruCentralNode）の上では false になる。ImGui が掴んでいる間
    // （ドラッグ・サイズ変更・スライダー）はカーソルがウィンドウの外に出ても true のまま
    const bool active = io.WantCaptureMouse || io.WantTextInput || ImGui::IsAnyItemActive();
    if (active)
    {
        s_platformIdleTime  = 0.0f;
        s_platformIdleAccum = 0.0f;
        return true;
    }

    s_platformIdleTime += io.DeltaTime;
    if (s_platformIdleTime < kPlatformActiveTail) return true;

    // 放置中は低い頻度で描く。Present を飛ばしたフレームは、DWM が前に受け取った絵を
    // そのまま出し続けるので、ちらつかない（ウィンドウが黒くなるのは、Present ごと止める
    // EditorContext::renderExternalWindows が false のときだけ）
    s_platformIdleAccum += io.DeltaTime;
    if (s_platformIdleAccum >= kPlatformIdleInterval)
    {
        s_platformIdleAccum = 0.0f;
        return true;
    }
    return false;
}

void ImGuiRenderer::SetPlatformWindowsEnabled(bool enabled)
{
    s_platformWindowsEnabled = enabled;
}

void ImGuiRenderer::SetPlatformWindowsThrottled(bool enabled)
{
    if (s_platformWindowsThrottled == enabled) return;
    s_platformWindowsThrottled = enabled;

    // 前に間引いていたときの経過時間が残っていると、切り替えた直後の描画が遅れて見える
    s_platformIdleTime  = 0.0f;
    s_platformIdleAccum = 0.0f;
}

// ---------------------------------------------------------------------------
// Win32 メッセージ
// ---------------------------------------------------------------------------

LRESULT ImGuiRenderer::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
}
