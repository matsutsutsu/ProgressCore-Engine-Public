#pragma once
// ===========================================================================
// 概要: ログを表示するコンソールウィンドウ。
//
// 責務:
//   - レベル・カテゴリ・文字列でログを絞り込んで表示する
//   - 1行・全体のクリップボードへのコピー
//   - 行のダブルクリックで Visual Studio の該当行を開く
//
// 備考: 表示するのは ImGuiBufferSink のリングバッファを Snapshot() で複製したもの。
//       描画中にロックを持ち続けると、ログを出す側のスレッドがその間止まるため。
//       カテゴリ名は GetLogCategoryName() だけを使う。ここに独自の対応表を持つと、
//       エンジン側にカテゴリを足したとき片方だけ古くなる。
// ===========================================================================
#include "Editor/Core/EditorWindow.h"
#include "Engine/Platform/Core/Logger.h"

#include <memory>
#include <string>

class ImGuiBufferSink;

class ConsoleWindow : public EditorWindow
{
public:
    ConsoleWindow();

    /**
     * @brief 表示に使うシンクを渡す。Framework の初期化時に呼ぶ。
     *
     * ImGuiBufferSink を共有し、UI 側はロックを取って複製する Snapshot() で履歴を読む。
     */
    void SetBufferSink(std::shared_ptr<ImGuiBufferSink> sink) { m_bufferSink = std::move(sink); }

protected:
    void DrawContents(EditorContext &context) override;

private:
    std::shared_ptr<ImGuiBufferSink> m_bufferSink;

    // レベルごとの表示
    bool m_showInfo    = true;
    bool m_showSuccess = true;
    bool m_showWarning = true;
    bool m_showError   = true;

    bool m_autoScroll = true;

    // カテゴリごとの表示（コンストラクタで全 true にする）
    bool m_categoryFilters[static_cast<int>(LogCategory::Count)] = {};

    char m_searchBuf[128] = {}; // 本文とファイル名を検索する文字列（大文字小文字を区別する）
};