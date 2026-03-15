// EditorWindow.h
#pragma once
#include <string>
#include <imgui.h>
#include "EditorContext.h"

// エディタウィンドウの基底クラス
class EditorWindow {
public:
    EditorWindow(const std::string& name) : _name(name), _isOpen(true) {}
    virtual ~EditorWindow() = default;

    // WindowManagerから呼ばれる描画の入り口
    void Render(EditorContext& context) {
        if (!_isOpen) return;

        if (ImGui::Begin(_name.c_str(), &_isOpen)) {
            DrawContents(context);
        }
        ImGui::End();
    }

    const std::string& GetName() const { return _name; }
    bool IsOpen() const { return _isOpen; }
    void SetOpen(bool open) { _isOpen = open; }

protected:
    // 各ウィンドウはこの関数をオーバーライドして中身を書く
    virtual void DrawContents(EditorContext& context) = 0;

    // クラスじゃなくてコンテキストにしてもいいかも

    std::string _name;
    bool _isOpen;
};