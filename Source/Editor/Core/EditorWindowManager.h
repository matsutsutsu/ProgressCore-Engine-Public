#pragma once
// ===========================================================================
// 概要: エディタのウィンドウ群とメインメニューバーを管理するクラス
//
// 責務:
//   - 全ウィンドウを登録・所有し、毎フレーム描画する
//   - メインメニューバー（File / Entity / Prefab / Scene / Window / Tools、一時停止、FPS）を描く
//   - エディタ全体のショートカット（Undo/Redo・F1・TAB・F11・F2）を処理する
//   - ゲームビューのフォーカスに応じて、マウス視点の可否を InputFacade へ伝える
//   - ウィンドウの表示状態を JSON に保存・復元する
// ===========================================================================
#include <vector>
#include <memory>
#include "EditorWindow.h"

class EditorWindowManager
{
public:
    EditorWindowManager() = default;

    // 全ウィンドウを生成して登録する
    void Initialize();

    // ショートカットの処理、メニューバーと全ウィンドウの描画を行う
    void Draw(EditorContext &context);

    // エディタの UI を出しているか（F1 で切り替える。false なら全画面でゲームを表示）
    bool IsVisible() const { return m_isVisible; }

    template <typename T>
    T *GetWindow()
    {
        for (auto &window : m_windows)
        {
            if (auto *ptr = dynamic_cast<T *>(window.get()))
            {
                return ptr;
            }
        }

        return nullptr;
    }

    // 各ウィンドウの表示状態を JSON に保存・復元する（ウィンドウ名で対応付ける）
    void SaveLayout(const std::string &path);
    void LoadLayout(const std::string &path);

private:
    void DrawMainMenuBar(EditorContext &context);

    // メニューバーの各メニュー
    void DrawFileMenu(EditorContext &context);
    void DrawEntityMenu(EditorContext &context);
    void DrawPrefabMenu(EditorContext &context);
    void DrawSceneMenu(EditorContext &context);
    void DrawWindowMenu(EditorContext &context);

    // 外部ツール（Tracy・ModelEditor）を起動するメニュー
    void DrawToolsMenu(EditorContext &context);

    void DrawPlayControlBar(EditorContext &context);      // 一時停止・コマ送りのボタン
    void DrawPerformanceStatsBar(EditorContext &context); // FPS 表示と、ホバー時の詳細

    // ウィンドウを生成して登録する。戻り値のポインタで、登録直後に設定を変えられる
    template <typename T, typename... Args>
    T *RegisterWindow(Args &&...args)
    {
        auto window = std::make_unique<T>(std::forward<Args>(args)...);
        T   *ptr    = window.get();
        m_windows.push_back(std::move(window));
        return ptr;
    }

    std::vector<std::unique_ptr<EditorWindow>> m_windows;

    bool m_isVisible = true; // エディタの UI 全体を出しているか
};
