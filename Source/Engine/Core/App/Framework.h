#pragma once
// ===========================================================================
// 概要: アプリ全体の起動・メインループ・終了を受け持つ最上位クラス Framework。
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
    /**
     * @brief エンジンの各サービスを初期化し、最初のシーンの生成を予約する。
     * @param hWnd 描画先のウィンドウ。所有しない（作るのは Main.cpp の wWinMain）。
     */
    Framework(HWND hWnd);
    ~Framework();

private:
    /**
     * @brief 固定ステップ1回分だけ、ロジックと物理を進める。
     * @param fixedTime 進める時間（秒）。常に FIXED_DT。
     * @note 物理・ロジックなど、フレームレートで結果が変わってはいけない処理だけをここで進める。
     * @note Run が1フレームに0〜4回（溜まった時間の分だけ）呼ぶ。一時停止中は呼ばれない。
     */
    void FixedUpdate(float fixedTime);

    /**
     * @brief 可変フレームの更新（シーンの Update と、エディタが参照する World の差し替え）。
     * @param elapsedTime 前フレームからの経過時間（秒）。エディタのデバッグ速度を掛けた値。
     */
    void Update(float elapsedTime);

    /**
     * @brief 1フレーム分を描画して Present する。
     * @param elapsedTime 前フレームからの経過時間（秒）。Update と同じ値。
     */
    void Render(float elapsedTime);

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
