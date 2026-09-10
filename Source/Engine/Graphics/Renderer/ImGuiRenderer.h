#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <imgui.h> // ImFont のために必要

class ImGuiRenderer
{
public:
    // 初期化
    static void Initialize(HWND hWnd, ID3D11Device *device, ID3D11DeviceContext *dc);

    // 終了化
    static void Finalize();

    // フレーム開始処理
    static void NewFrame();

    // 描画実行
    static void Render(ID3D11DeviceContext *context);

    // マルチビューポート（切り離し OS 窓）のセカンダリ描画/Present を許可するか。
    // Play 中や F1 で UI 非表示のときは false にし、セカンダリ Present を抑止する。
    // ViewportsEnable 自体は切らない（DestroyPlatformWindows はシャットダウン専用のため）。
    static void SetPlatformWindowsEnabled(bool enabled);
    static bool ArePlatformWindowsEnabled() { return s_platformWindowsEnabled; }

    // WIN32メッセージハンドラー
    static LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // エディタ用フォントを取得する関数
    static ImFont *GetEditorFont() { return s_editorFont; }

private:
    static ImFont *s_editorFont; // 保持用ポインタ
    static bool    s_platformWindowsEnabled;
};
