#pragma once
// ===========================================================================
// 概要: アプリ全体の起動・メインループ・終了を受け持つ最上位クラス Framework
//
// 責務:
//   - エンジンの各サービス（ログ・描画・入力・音・物理・シーン・エディタ）の初期化と破棄
//   - 固定タイムステップ（FixedUpdate）と可変フレーム（Update/Render）のメインループ
//   - Win32 メッセージの処理
// ===========================================================================
#include <windows.h>
#include "Engine/Core/App/HighResolutionTimer.h"

#ifdef ENABLE_EDITOR
#include "Editor/Core/EditorContext.h"
#endif

class EditorWindowManager;
class ImGuiBufferSink;

class Framework
{
public:
    // エンジンの各サービスを初期化し、最初のシーンの生成を予約する。hWnd は所有しない
    Framework(HWND hWnd);
    ~Framework();

private:
    // 固定ステップ1回分（FIXED_DT）だけ、ロジックと物理を進める
    // Run が1フレームに0〜4回呼ぶ（溜まった時間の分だけ）。一時停止中は呼ばれない
    void FixedUpdate(float fixedTime);

    // シーンの Update と、エディタが見る World の差し替え。elapsedTime はエディタのデバッグ速度を掛けた値
    void Update(float elapsedTime);

    // 1フレーム分を描いて Present する
    void Render(float elapsedTime);

    // ビルド日時・OS・CPU・メモリと、Assets・Data.pak の有無を Shipping でもログに書く
    // GPU と画面は GraphicsDevice が書く（デバイスを作った後でないと分からない）
    void LogPlatformInfo() const;

public:
    // メインループ。固定タイムステップは Accumulator パターン（Fix Your Timestep）で回す
    int Run();

    // ウィンドウプロシージャの本体（Main.cpp の fnWndProc から転送される）
    LRESULT CALLBACK HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    const HWND          hWnd;  // 描画先のウィンドウ（所有しない）
    HighResolutionTimer timer; // フレーム間の経過時間の計測

    static constexpr float FIXED_DT = 1.0f / 60.0f; // 固定ステップの長さ（秒）

    // --- ウィンドウタイトルへの計測表示 ---

    int   m_fixedUpdateCounter = 0;    // 直近1秒間の FixedUpdate 回数
    float m_debugTimer         = 0.0f; // 1秒の経過計測（秒）

    std::shared_ptr<ImGuiBufferSink> m_consoleLogSink; // コンソールウィンドウと共有するログシンク

    // --- エディタ層 ---
#ifdef ENABLE_EDITOR
    std::unique_ptr<EditorWindowManager> m_editorWindowManager; // エディタの全ウィンドウ
    EditorContext                        m_editorContext;       // エディタとゲームの間で受け渡す状態（選択・一時停止・ロード要求など）
    uint32_t                             m_lastSceneSerial = 0; // 直近に見たシーン切り替えの通し番号。変わったら Undo 履歴と選択を捨てる
#endif
};
