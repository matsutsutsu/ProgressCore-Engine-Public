#include <windows.h>
#include <memory>
#include <assert.h>
#include <tchar.h>
#include <time.h>
#include <filesystem>

#include "Framework.h"
#include "Resources/Resource.h"

// ハイブリッド GPU の PC で、Windows が内蔵 GPU を選ぶのを防ぐ。ドライバはこのシンボルを高性能 GPU の要求と読む
// GraphicsDevice::Initialize のアダプタの選択と合わせた二重の対策（こちらはドライバ側の受け皿）
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement                  = 0x00000001;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
}

#define SCREEN_MODE false // true: フルスクリーン、false: ウィンドウモード

// フルスクリーン時の解像度（ウィンドウモードはモニタの解像度に合わせる）
const LONG SCREEN_WIDTH  = 1920;
const LONG SCREEN_HEIGHT = 1080;

#if SCREEN_MODE
const BOOL FULLSCREEN = TRUE;
#else
const BOOL FULLSCREEN = FALSE;
#endif

// 作業フォルダを exe のあるフォルダへ合わせる。ファイルはすべて作業フォルダからの相対パスで開くため
// exe の隣に Assets がなければ、元の作業フォルダのままにする（開発中は VS がリポジトリのルートで起動する）
static void SetWorkingDirectoryToExe()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD len        = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return; // 取れない・長すぎるときは何もしない

    const std::filesystem::path exeDir = std::filesystem::path(path).parent_path();
    std::error_code             ec;
    if (std::filesystem::is_directory(exeDir / L"Assets", ec))
        std::filesystem::current_path(exeDir, ec);
}



// ウィンドウに紐付けた Framework へメッセージを転送する
LRESULT CALLBACK fnWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    Framework *f = reinterpret_cast<Framework *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    return f ? f->HandleMessage(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}



INT WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, LPWSTR cmd_line, INT cmd_show)
{
    // 処理フロー:
    // 1. リーク検出と作業フォルダを設定する
    // 2. ウィンドウクラスを登録する
    // 3. ウィンドウを作る（フルスクリーン、またはボーダーレス）
    // 4. Framework を作り、メインループを回す

#if defined(DEBUG) || defined(_DEBUG)
    // 終了時にメモリリークをダンプする
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    SetWorkingDirectoryToExe(); // ファイルを読む前に合わせる（ログ・設定・Assets のすべてがこれを基準にする）

    WNDCLASSEX wcex;
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc   = fnWndProc;
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = 0;
    wcex.hInstance     = instance;
    wcex.hIcon         = LoadIcon(instance, MAKEINTRESOURCE(IDI_APP_ICON)); // タスクバー・Alt+Tab 用（大）
    wcex.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName  = NULL;
    wcex.lpszClassName = _T("Game");
    wcex.hIconSm       = static_cast<HICON>(LoadImage(instance, MAKEINTRESOURCE(IDI_APP_ICON), IMAGE_ICON,
                                                      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                      LR_DEFAULTCOLOR)); // タイトルバー用（小）。.ico から合うサイズを選ばせる
    RegisterClassEx(&wcex);

    srand(static_cast<unsigned int>(time(nullptr)));

    HWND hWnd;
    if (FULLSCREEN)
    {
        DEVMODE devMode      = {};
        devMode.dmSize       = sizeof(DEVMODE);
        devMode.dmPelsWidth  = SCREEN_WIDTH;
        devMode.dmPelsHeight = SCREEN_HEIGHT;
        devMode.dmBitsPerPel = 32;
        devMode.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;

        ChangeDisplaySettings(&devMode, CDS_FULLSCREEN);

        DWORD style   = WS_POPUP | WS_VISIBLE;
        DWORD exStyle = 0; // WS_EX_TOPMOST は付けない（常に最前面に残り、他のウィンドウを隠してしまう）

        hWnd = CreateWindowEx(exStyle, _T("Game"), _T("OVERCLEAVE"), style,
                              0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
                              NULL, NULL, instance, NULL);
    }
    else
    {
        // ボーダーレスウィンドウ（モニタ全体を覆う、タイトルバーも枠もないウィンドウ）
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        DWORD style = WS_POPUP | WS_VISIBLE;

        // 枠がないので、クライアント領域を合わせる AdjustWindowRect は要らない
        hWnd = CreateWindowEx(
            0,
            _T("Game"),
            _T("OVERCLEAVE"),
            style,
            0, 0,    // X, Y座標
            screenW, // 幅
            screenH, // 高さ
            NULL, NULL, instance, NULL);
    }

    // fnWndProc が Framework へ転送できるよう、ウィンドウに Framework のアドレスを持たせる
    Framework f(hWnd);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&f));
    return f.Run();
}
