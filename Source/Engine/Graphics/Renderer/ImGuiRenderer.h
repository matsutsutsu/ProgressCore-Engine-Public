#pragma once
// ===========================================================================
// 概要: Dear ImGui の初期化・フレームの開始・描画をまとめた static クラス。
//
// 責務:
//   - ImGui のコンテキストと Win32 / DX11 のバックエンドを作る・壊す
//   - 毎フレーム、画面全体のドックスペースを作る
//   - 切り離した OS ウィンドウ（マルチビューポート）の描画を、操作していない間は間引く
//
// 備考: エディタ入りのビルド（ENABLE_EDITOR）でだけ使う。
// ===========================================================================
#include <Windows.h>
#include <d3d11.h>
#include <imgui.h> // ImFont

class ImGuiRenderer
{
public:
    static void Initialize(HWND hWnd, ID3D11Device *device, ID3D11DeviceContext *dc);
    static void Finalize();
    static void NewFrame();
    static void Render(ID3D11DeviceContext *context);

    /**
     * @brief 切り離した OS ウィンドウを描いて Present するかを切り替える。
     *
     * ViewportsEnable 自体は切らない。切るには DestroyPlatformWindows が要り、それはシャットダウン専用のため。
     *
     * @param enabled 描くなら true。Play 中や F1 で UI を隠したときに false にする。
     */
    static void SetPlatformWindowsEnabled(bool enabled);
    static bool ArePlatformWindowsEnabled() { return s_platformWindowsEnabled; }

    /**
     * @brief 操作していない間、切り離した OS ウィンドウの描画を間引くかを切り替える。
     *
     * ImGui は毎フレーム全部を作り直すので、間引かないと触っていないウィンドウもゲームと同じ頻度で描いて Present する。
     * 別のモニタにウィンドウを出したときの負荷が大きく下がる。
     *
     * @param enabled 間引くなら true。
     */
    static void SetPlatformWindowsThrottled(bool enabled);
    static bool ArePlatformWindowsThrottled() { return s_platformWindowsThrottled; }

    // Win32 のメッセージを ImGui に渡す
    static LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    // このフレームで、切り離した OS ウィンドウを描き直すか（間引きの判定）
    static bool ShouldRenderPlatformWindows();

    static bool s_platformWindowsEnabled;

    static bool  s_platformWindowsThrottled; // 間引くか
    static float s_platformIdleTime;         // 操作が途切れてからの時間（秒）
    static float s_platformIdleAccum;        // 前に描いてからの時間（秒。放置中に低い頻度で描くため）
    static int   s_platformViewportCount;    // 前のフレームの OS ウィンドウの数（増減を見る）
};
