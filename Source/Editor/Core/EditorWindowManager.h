// EditorWindowManager.h
#pragma once
#include <vector>
#include <memory>
#include "EditorWindow.h"

class EditorWindowManager {
public:
    EditorWindowManager() = default;

	// ウィンドウの初期化と登録
    void Initialize();

    // 全ウィンドウの一括描画
    void Draw(EditorContext &context);

    bool IsVisible() const { return m_isVisible; }


    template<typename T>
    T* GetWindow()
    {
        for (auto& window : m_windows)
        {
            if (auto* ptr = dynamic_cast<T*>(window.get()))
            {
                return ptr;
            }
        }

        return nullptr;
    }

    // Windowの可視状態をデータ化
    void SaveLayout(const std::string& path);
    void LoadLayout(const std::string& path);

private:

    void DrawMainMenuBar(EditorContext& context);

    // =========================================================
    // メニューバーを構成する各部品を関数に分割
    // =========================================================
    void DrawFileMenu(EditorContext& context);
    void DrawEntityMenu(EditorContext& context);
    void DrawPrefabMenu(EditorContext& context);
    void DrawSceneMenu(EditorContext& context);
    void DrawWindowMenu(EditorContext& context);

    // 外部ツール起動用のメニュー
    void DrawToolsMenu(EditorContext& context);

    void DrawPlayControlBar(EditorContext& context);
    void DrawPerformanceStatsBar(EditorContext& context);


    // ウィンドウの登録
    template<typename T, typename... Args>
    T* RegisterWindow(Args&&... args) {
        auto window = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = window.get();
        m_windows.push_back(std::move(window));
        return ptr; // ポインタを返すことで、登録した直後に設定をいじれるようになる
    }

 

    std::vector<std::unique_ptr<EditorWindow>> m_windows;

    // UI全体の表示フラグ
    bool m_isVisible = true;
};