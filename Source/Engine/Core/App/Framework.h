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
    Framework(HWND hWnd);
    ~Framework();

private:
    // 固定更新（物理・ロジック用）：1/60秒ごとに呼ばれる
    void FixedUpdate(float fixedTime);

    // 可変更新（描画準備・入力用）：毎フレーム呼ばれる
    void Update(float elapsedTime);

    // 描画実行：毎フレーム呼ばれる
    void Render(float elapsedTime);

public:
    // メインループ。固定タイムステップは Accumulator パターン（Fix Your Timestep）で回す
    int              Run();
    LRESULT CALLBACK HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    const HWND          hWnd;
    HighResolutionTimer timer;

    // 固定タイムステップ（60FPS = 0.01666...秒）
    static constexpr float FIXED_DT = 1.0f / 60.0f;

    // ウィンドウタイトルへの計測表示用
    int   m_fixedUpdateCounter = 0;    // 直近1秒間の FixedUpdate 回数
    float m_debugTimer         = 0.0f; // 1秒の経過計測（秒）
    int   m_displayFixedCount  = 0;    // 表示用の確定値

    std::shared_ptr<ImGuiBufferSink> m_consoleLogSink; // コンソールウィンドウと共有するログシンク

// エディタ層のインスタンス群
#ifdef ENABLE_EDITOR
    std::unique_ptr<EditorWindowManager> m_editorWindowManager;
    EditorContext                        m_editorContext;

    // 直近に見た SceneManager の切替通し番号。
    //   これが変わったフレームで Undo 履歴と選択を破棄する（Framework.cpp を参照）。
    uint32_t m_lastSceneSerial = 0;
#endif
};
