#include <windows.h>
#include <memory>
#include <assert.h>
#include <tchar.h>
#include <time.h>

#include "Framework.h"


//#include "PxPhysicsAPI.h"
//#pragma comment(lib, "PhysX_64.lib")
//#pragma comment(lib, "PhysXCommon_64.lib")
//#pragma comment(lib, "PhysXCooking_64.lib")
//#pragma comment(lib, "PhysXExtensions_static_64.lib")
//#pragma comment(lib, "PhysXFoundation_64.lib")
//#pragma comment(lib, "PhysXPvdSDK_static_64.lib")
//#pragma comment(lib, "PhysXTask_static_64.lib")
//#pragma comment(lib, "SceneQuery_static_64.lib")
//#pragma comment(lib, "SimulationController_static_64.lib")


// ===================================================================
// ハイブリッドGPU(Optimus/PowerXpress)搭載PCで、Windows が内蔵GPU(Intel等)を
// 自動選択してしまう問題への対策。ドライバはexe側がこのシンボルをexportして
// いるかを見て「高性能GPUを使いたい」という意思表示だと解釈する。
// GraphicsDevice::Initialize 側の IDXGIFactory6::EnumAdapterByGpuPreference
// による明示的なアダプタ選択と合わせた二重対策（こちらはドライバ側のフォールバック）。
// ===================================================================
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
}

#define SCREEN_MODE false // true: フルスクリーン、false: ウィンドウモード


const LONG SCREEN_WIDTH = 1920;
const LONG SCREEN_HEIGHT = 1080;
//const LONG SCREEN_WIDTH = 1280;
//const LONG SCREEN_HEIGHT = 720;

#if SCREEN_MODE
const BOOL FULLSCREEN = TRUE;
#else
const BOOL FULLSCREEN = FALSE;
#endif 

LRESULT CALLBACK fnWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	Framework *f = reinterpret_cast<Framework*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	return f ? f->HandleMessage(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}




INT WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, LPWSTR cmd_line, INT cmd_show)
{
#if defined(DEBUG) || defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	//_CrtSetBreakAlloc(237);
#endif
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = fnWndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = instance;
	wcex.hIcon = 0;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = _T("Game");
	wcex.hIconSm = 0;
	RegisterClassEx(&wcex);

    srand(static_cast<unsigned int>(time(nullptr)));

	HWND hWnd;
	if (FULLSCREEN)
	{
		DEVMODE devMode = {};
		devMode.dmSize = sizeof(DEVMODE);
		devMode.dmPelsWidth = SCREEN_WIDTH;
		devMode.dmPelsHeight = SCREEN_HEIGHT;
		devMode.dmBitsPerPel = 32;
		devMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;

		ChangeDisplaySettings(&devMode, CDS_FULLSCREEN);

		DWORD style = WS_POPUP | WS_VISIBLE;
		DWORD exStyle = 0; // WS_EX_TOPMOSTは付けない！

		hWnd = CreateWindowEx(exStyle, _T("Game"), _T("OVERCLEAVE"), style,
			0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
			NULL, NULL, instance, NULL);
	}
	else
	{
		// ---------------------------------------------------------
		// ボーダーレスウィンドウの実装
		// ---------------------------------------------------------
		// 1. 画面サイズをシステムから取得
		int screenW = GetSystemMetrics(SM_CXSCREEN);
		int screenH = GetSystemMetrics(SM_CYSCREEN);

		// 2. ボーダーレス用のスタイル (タイトルバーなし、枠なし)
		DWORD style = WS_POPUP | WS_VISIBLE;

		// 3. ウィンドウ作成 (枠がないため AdjustedWindowRect は不要)
		hWnd = CreateWindowEx(
			0,
			_T("Game"),
			_T("OVERCLEAVE"),
			style,
			0, 0,           // X, Y座標
			screenW,        // 幅
			screenH,        // 高さ
			NULL, NULL, instance, NULL
		);

	}

	Framework f(hWnd);
	SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&f));
	return f.Run();
}
