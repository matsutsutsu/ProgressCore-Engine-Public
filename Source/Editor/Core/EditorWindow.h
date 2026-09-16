// EditorWindow.h
#pragma once
#include <string>
#include "EditorContext.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

#include "tracy/Tracy.hpp"

// エディタウィンドウの基底クラス
class EditorWindow
{
public:
    EditorWindow(const std::string &name, bool defaultVisible = true) : m_name(name), m_isVisible(defaultVisible) {}
    virtual ~EditorWindow() = default;

    // WindowManagerから呼ばれる描画の入り口
    void Render(EditorContext &context)
    {
        // 表示状態に関係なく毎フレーム走る。ワールド側へ効き続ける状態（編集モード等）を
        // 持つウィンドウは、閉じられた/畳まれた瞬間に自分で後始末しないと状態が焼き付く。
        OnFrameBegin(context);

        if (!m_isVisible) return;

        ZoneScoped; // 無名スコープを作成
        // Tracy上で見つけやすいようにプレフィックスを付ける
        std::string zoneName = "UI Window: " + m_name;
        ZoneName(zoneName.c_str(), zoneName.size());

#ifdef ENABLE_EDITOR

        if (ImGui::Begin(m_name.c_str(), &m_isVisible))
        {
            DrawContents(context);
        }
        ImGui::End();
#endif
    }

    const std::string &GetName() const { return m_name; }
    bool               GetVisible() const { return m_isVisible; }
    void               SetVisible(bool open) { m_isVisible = open; }

protected:
    // 各ウィンドウはこの関数をオーバーライドして中身を書く
    virtual void DrawContents(EditorContext &context) = 0;

    // 非表示・折りたたみ・DrawContents の早期returnでも必ず呼ばれる。
    // 「描画されている間しか維持できない状態」の解除に使う。
    virtual void OnFrameBegin(EditorContext & /*context*/) {}

    // クラスじゃなくてコンテキストにしてもいいかも

    std::string m_name;
    bool        m_isVisible;
};