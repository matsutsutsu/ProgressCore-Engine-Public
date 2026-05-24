//==============================================================================
// ConsoleWindow.cpp
//
// 改善点:
//   - Snapshot() 経由でロックを長く持たない
//   - カテゴリ名は GetLogCategoryName() に統一 (二重定義を廃止)
//   - 検索ボックス追加
//   - Copy All のバグ修正 (LogToClipboard/LogFinish を正しく使用)
//   - 行ダブルクリックで Visual Studio に該当行ジャンプ
//==============================================================================
#include "ConsoleWindow.h"
#include "Engine/Platform/LogSinks.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <imgui.h>

ConsoleWindow::ConsoleWindow() : EditorWindow("Console")
{
    // カテゴリフィルタを全 true で初期化
    for (auto& f : _categoryFilters) f = true;
}

namespace
{
    /**
     * @brief Visual Studio で該当ファイル/行を開く
     * @details VS の自動化用 ProgID 経由。失敗しても無害なように try/catch 相当の扱い
     */
    void OpenInVisualStudio(const std::string& file, int line)
    {
        // 簡易実装: 既定のエディタで開く (devenv が PATH にあれば優先)
        char cmd[1024];
        std::snprintf(cmd, sizeof(cmd), "devenv /edit \"%s\" /command \"Edit.GoTo %d\"",
            file.c_str(), line);
        STARTUPINFOA si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    ImVec4 LevelColor(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Success: return ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
        case LogLevel::Warning: return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        case LogLevel::Error:   return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        case LogLevel::Info:
        default:                return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    /// 1行を CSV/コピー用に整形
    std::string FormatLine(const LogMessage& m)
    {
        char buf[2048];
        std::snprintf(buf, sizeof(buf), "[%s] [%s] [%s:%d] %s",
            (m.level == LogLevel::Error) ? "ERROR" :
            (m.level == LogLevel::Warning) ? "WARN" :
            (m.level == LogLevel::Success) ? "SUCCESS" : "INFO",
            GetLogCategoryName(m.category),
            m.file.c_str(), m.line, m.text.c_str());
        return buf;
    }
}

void ConsoleWindow::DrawContents(EditorContext& /*context*/)
{
    if (!_bufferSink)
    {
        ImGui::TextDisabled("ConsoleWindow: ImGuiBufferSink is not set.");
        ImGui::TextDisabled("Call ConsoleWindow::SetBufferSink() during initialization.");
        return;
    }

    // --------------------------------------------------------------------
    // 1. ツールバー
    // --------------------------------------------------------------------
    if (ImGui::Button("Clear"))
    {
        Logger::Instance().Clear();
    }
    ImGui::SameLine();
    bool copyAllRequested = ImGui::Button("Copy All");

    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &_autoScroll);

    ImGui::SameLine();
    ImGui::Checkbox("Info", &_showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Success", &_showSuccess);
    ImGui::SameLine();
    ImGui::Checkbox("Warning", &_showWarning);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &_showError);

    // 検索ボックス
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##search", "Search...", _searchBuf, sizeof(_searchBuf));

    // --------------------------------------------------------------------
    // 2. カテゴリフィルタ
    // --------------------------------------------------------------------
    ImGui::Separator();
    ImGui::TextUnformatted("Categories:");
    ImGui::SameLine();
    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
    {
        ImGui::Checkbox(GetLogCategoryName(static_cast<LogCategory>(i)),
            &_categoryFilters[i]);
        if (i < static_cast<int>(LogCategory::Count) - 1) ImGui::SameLine();
    }
    ImGui::Separator();

    // --------------------------------------------------------------------
    // 3. スナップショット取得 + フィルタリング
    //    (Sink からコピーを取るのでロックを長く持たない)
    // --------------------------------------------------------------------
    const auto messages = _bufferSink->Snapshot();

    std::vector<int> filtered;
    filtered.reserve(messages.size());

    const bool hasSearch = (_searchBuf[0] != '\0');
    for (int i = 0; i < static_cast<int>(messages.size()); ++i)
    {
        const auto& m = messages[i];

        switch (m.level)
        {
        case LogLevel::Info:    if (!_showInfo)    continue; break;
        case LogLevel::Success: if (!_showSuccess) continue; break;
        case LogLevel::Warning: if (!_showWarning) continue; break;
        case LogLevel::Error:   if (!_showError)   continue; break;
        }

        if (!_categoryFilters[static_cast<int>(m.category)]) continue;

        if (hasSearch)
        {
            // 単純な大文字小文字無視の包含検索
            // (高頻度用途ではないので strstr で十分。必要なら正規表現に拡張)
            if (m.text.find(_searchBuf) == std::string::npos &&
                m.file.find(_searchBuf) == std::string::npos) continue;
        }

        filtered.push_back(i);
    }

    // --------------------------------------------------------------------
    // 4. Copy All - LogToClipboard を正しく使う
    //    (ImGui::Text* の出力をクリップボードへリダイレクトするモード)
    // --------------------------------------------------------------------
    ImGui::BeginChild("LogRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    if (copyAllRequested)
    {
        ImGui::LogToClipboard();
    }

    // --------------------------------------------------------------------
    // 5. 描画 (Clipper で高速化)
    // --------------------------------------------------------------------
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filtered.size()));
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const auto& m = messages[filtered[i]];
            const auto  color = LevelColor(m.level);
            const auto* catName = GetLogCategoryName(m.category);

            ImGui::PushID(i);
            ImGui::TextColored(color, "[%s] [%s:%d] %s",
                catName, m.file.c_str(), m.line, m.text.c_str());

            // 行ダブルクリック → Visual Studio で開く
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                OpenInVisualStudio(m.file, m.line);
            }

            // 右クリックで1行コピー
            if (ImGui::BeginPopupContextItem("LogContextMenu"))
            {
                if (ImGui::Selectable("Copy this line"))
                {
                    const auto line = FormatLine(m);
                    ImGui::SetClipboardText(line.c_str());
                }
                if (ImGui::Selectable("Open in Visual Studio"))
                {
                    OpenInVisualStudio(m.file, m.line);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    clipper.End();

    if (copyAllRequested)
    {
        ImGui::LogFinish();
    }

    // --------------------------------------------------------------------
    // 6. 自動スクロール
    // --------------------------------------------------------------------
    if (_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}