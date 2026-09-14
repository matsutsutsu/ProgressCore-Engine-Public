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

    // 操作していない間、切り離し OS 窓の再描画を間引くか。
    //   ImGui は「毎フレーム全部作り直す」設計なので、既定では触っていない窓も
    //   ゲームと同じ 60fps で再描画＆Present される。ここを落とすと、別モニタへ
    //   窓を出したときの描画スレッドの負荷が大きく下がる。
    //   OFF にすると常時フル描画（従来の挙動）。
    static void SetPlatformWindowsThrottled(bool enabled);
    static bool ArePlatformWindowsThrottled() { return s_platformWindowsThrottled; }

    // WIN32メッセージハンドラー
    static LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // エディタ用フォントを取得する関数
    static ImFont *GetEditorFont() { return s_editorFont; }

private:
    // 今フレーム、切り離し OS 窓を描き直すべきか（間引きの判定本体）。
    static bool ShouldRenderPlatformWindows();

    static ImFont *s_editorFont; // 保持用ポインタ
    static bool    s_platformWindowsEnabled;

    // --- 間引きの状態 ---
    static bool  s_platformWindowsThrottled; // 間引きを行うか
    static float s_platformIdleTime;         // 操作が途切れてからの経過秒
    static float s_platformIdleAccum;        // 放置中の低頻度描画のためのたまり
    static int   s_platformViewportCount;    // 前フレームの OS 窓の数（増減検出用）
};
