// EditorWindow.h
#pragma once
#include <string>
#include "EditorContext.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

#include "tracy/Tracy.hpp"

// エディタウィンドウの基底クラス
class EditorWindow {
public:
    EditorWindow(const std::string& name, bool defaultVisible = true) : m_name(name), m_isVisible(defaultVisible) {}
    virtual ~EditorWindow() = default;

    // WindowManagerから呼ばれる描画の入り口
    void Render(EditorContext& context) {
        if (!m_isVisible) return;

		
        ZoneScoped; // 無名スコープを作成
        // Tracy上で見つけやすいようにプレフィックスを付ける
        std::string zoneName = "UI Window: " + m_name;
        ZoneName(zoneName.c_str(), zoneName.size());

#ifdef ENABLE_EDITOR

        if (ImGui::Begin(m_name.c_str(), &m_isVisible)) {
            DrawContents(context);
        }
        ImGui::End();
#endif
    }

    const std::string& GetName() const { return m_name; }
    bool GetVisible() const { return m_isVisible; }
    void SetVisible(bool open) { m_isVisible = open; }

protected:
    // 各ウィンドウはこの関数をオーバーライドして中身を書く
    virtual void DrawContents(EditorContext& context) = 0;

    // クラスじゃなくてコンテキストにしてもいいかも

    std::string m_name;
    bool m_isVisible;
};