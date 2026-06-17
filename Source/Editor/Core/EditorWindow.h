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
    EditorWindow(const std::string& name, bool defaultVisible = true) : _name(name), _isVisible(defaultVisible) {}
    virtual ~EditorWindow() = default;

    // WindowManagerから呼ばれる描画の入り口
    void Render(EditorContext& context) {
        if (!_isVisible) return;

		
        ZoneScoped; // 無名スコープを作成
        // Tracy上で見つけやすいようにプレフィックスを付ける
        std::string zoneName = "UI Window: " + _name;
        ZoneName(zoneName.c_str(), zoneName.size());

#ifdef ENABLE_EDITOR

        if (ImGui::Begin(_name.c_str(), &_isVisible)) {
            DrawContents(context);
        }
        ImGui::End();
#endif
    }

    const std::string& GetName() const { return _name; }
    bool GetVisible() const { return _isVisible; }
    void SetVisible(bool open) { _isVisible = open; }

protected:
    // 各ウィンドウはこの関数をオーバーライドして中身を書く
    virtual void DrawContents(EditorContext& context) = 0;

    // クラスじゃなくてコンテキストにしてもいいかも

    std::string _name;
    bool _isVisible;
};