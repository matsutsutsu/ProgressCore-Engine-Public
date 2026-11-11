#pragma once
// ===========================================================================
// 概要: エディタのウィンドウ（ImGui のウィンドウ1枚）の基底クラス
//
// 責務:
//   - 表示状態を持ち、表示中だけ ImGui::Begin/End で囲んで DrawContents を呼ぶ
//   - 表示状態に関係なく毎フレーム OnFrameBegin を呼ぶ
//   - Tracy のゾーンにウィンドウ名を付ける
//
// 備考: 各ウィンドウは DrawContents をオーバーライドして中身を書く
//       EditorWindowManager が所有し、毎フレーム Render を呼ぶ
// ===========================================================================
#include <string>
#include "EditorContext.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

#include "tracy/Tracy.hpp"

class EditorWindow
{
public:
    EditorWindow(const std::string &name, bool defaultVisible = true) : m_name(name), m_isVisible(defaultVisible) {}
    virtual ~EditorWindow() = default;

    // EditorWindowManager から毎フレーム呼ばれる描画の入口
    void Render(EditorContext &context)
    {
        // 表示状態に関係なく毎フレーム呼ぶ。ワールド側へ効き続ける状態（編集モード等）を
        // 持つウィンドウは、閉じられた・畳まれた瞬間に自分で後始末しないと、その状態が残り続ける
        OnFrameBegin(context);

        if (!m_isVisible) return;

        ZoneScoped;
        // Tracy 上で見つけやすいよう、ゾーン名にプレフィックスを付ける
        std::string zoneName = "UI Window: " + m_name;
        ZoneName(zoneName.c_str(), zoneName.size());

#ifdef ENABLE_EDITOR
        OnBeforeBegin();

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

    // 非表示・折りたたみ・DrawContents の早期 return でも必ず呼ばれる
    // 「描画されている間しか維持できない状態」の解除に使う
    virtual void OnFrameBegin(EditorContext & /*context*/) {}

    // 表示中だけ、ImGui::Begin の直前に呼ばれる。ImGui::SetNextWindowSize などで初期の大きさを決めるのに使う
    virtual void OnBeforeBegin() {}

    std::string m_name;      // ウィンドウ名（ImGui の ID とレイアウト保存のキーを兼ねる）
    bool        m_isVisible; // 表示中か
};