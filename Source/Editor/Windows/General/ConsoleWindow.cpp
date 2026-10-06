#include "Editor/Windows/General/ConsoleWindow.h"
#include "Engine/Platform/Core/LogSinks.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <imgui.h>

ConsoleWindow::ConsoleWindow() : EditorWindow("Console")
{
    // カテゴリフィルタを全 true で初期化
    for (auto &f : m_categoryFilters)
        f = true;
}

namespace
{
    /**
     * @brief devenv のコマンドラインで、Visual Studio に該当ファイル・行を開かせる。
     * @param file 開くファイル。
     * @param line 開く行（1始まり）。
     * @note devenv が PATH に無ければ CreateProcess が失敗し、何も起きない。
     */
    void OpenInVisualStudio(const std::string &file, int line)
    {
        char cmd[1024];
        std::snprintf(cmd, sizeof(cmd), "devenv /edit \"%s\" /command \"Edit.GoTo %d\"",
                      file.c_str(), line);
        STARTUPINFOA si{};
        si.cb = sizeof(si);
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
        case LogLevel::Success:
            return ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
        case LogLevel::Warning:
            return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        case LogLevel::Error:
            return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        case LogLevel::Info:
        default:
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    // 1行をコピー用の文字列にする
    std::string FormatLine(const LogMessage &m)
    {
        char buf[2048];
        std::snprintf(buf, sizeof(buf), "[%s] [%s] [%s:%d] %s",
                      (m.level == LogLevel::Error) ? "ERROR" : (m.level == LogLevel::Warning) ? "WARN"
                                                           : (m.level == LogLevel::Success)   ? "SUCCESS"
                                                                                              : "INFO",
                      GetLogCategoryName(m.category),
                      m.file.c_str(), m.line, m.text.c_str());
        return buf;
    }
} // namespace

void ConsoleWindow::DrawContents(EditorContext & /*context*/)
{
    if (!m_bufferSink)
    {
        ImGui::TextDisabled("ConsoleWindow: ImGuiBufferSink is not set.");
        ImGui::TextDisabled("Call ConsoleWindow::SetBufferSink() during initialization.");
        return;
    }

    // ---- ツールバー ----
    if (ImGui::Button("Clear"))
    {
        Logger::Instance().Clear();
    }
    ImGui::SameLine();
    bool copyAllRequested = ImGui::Button("Copy All");

    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &m_autoScroll);

    ImGui::SameLine();
    ImGui::Checkbox("Info", &m_showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Success", &m_showSuccess);
    ImGui::SameLine();
    ImGui::Checkbox("Warning", &m_showWarning);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &m_showError);

    // 検索ボックス
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##search", "Search...", m_searchBuf, sizeof(m_searchBuf));

    // ---- カテゴリの絞り込み ----
    ImGui::Separator();
    ImGui::TextUnformatted("Categories:");
    ImGui::SameLine();
    for (int i = 0; i < static_cast<int>(LogCategory::Count); ++i)
    {
        ImGui::Checkbox(GetLogCategoryName(static_cast<LogCategory>(i)),
                        &m_categoryFilters[i]);
        if (i < static_cast<int>(LogCategory::Count) - 1) ImGui::SameLine();
    }
    ImGui::Separator();

    // ---- Snapshot の取得と絞り込み ----
    // 0.1 秒に1回だけ Snapshot を取り直し、毎フレーム全ログを複製するのを避ける。
    // 絞り込みの結果（s_cachedFiltered）も同じタイミングで作り直す。
    static std::vector<LogMessage> s_cachedMessages;
    static std::vector<int>        s_cachedFiltered; // 表示する行の s_cachedMessages での添字
    static float                   s_updateTimer = 0.0f;

    s_updateTimer += ImGui::GetIO().DeltaTime;
    if (s_updateTimer > 0.1f)
    {
        s_cachedMessages = m_bufferSink->Snapshot();
        s_cachedFiltered.clear();
        s_cachedFiltered.reserve(s_cachedMessages.size());

        const bool hasSearch = (m_searchBuf[0] != '\0');
        for (int i = 0; i < static_cast<int>(s_cachedMessages.size()); ++i)
        {
            const auto &m = s_cachedMessages[i];

            switch (m.level)
            {
            case LogLevel::Info:
                if (!m_showInfo) continue;
                break;
            case LogLevel::Success:
                if (!m_showSuccess) continue;
                break;
            case LogLevel::Warning:
                if (!m_showWarning) continue;
                break;
            case LogLevel::Error:
                if (!m_showError) continue;
                break;
            }
            if (!m_categoryFilters[static_cast<int>(m.category)]) continue;

            if (hasSearch)
            {
                if (m.text.find(m_searchBuf) == std::string::npos &&
                    m.file.find(m_searchBuf) == std::string::npos) continue;
            }

            s_cachedFiltered.push_back(i);
        }
        s_updateTimer = 0.0f;
    }

    // 絞り込み後の全行をコピーする（画面に見えている行だけではない）
    if (copyAllRequested)
    {
        std::string all;
        for (int idx : s_cachedFiltered)
        {
            all += FormatLine(s_cachedMessages[idx]);
            all += '\n';
        }
        ImGui::SetClipboardText(all.c_str());
    }

    // ---- ログの表示（ListClipper で見えている行だけを描く） ----
    ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(s_cachedFiltered.size()));
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const auto &m       = s_cachedMessages[s_cachedFiltered[i]];
            const auto  color   = LevelColor(m.level);
            const auto *catName = GetLogCategoryName(m.category);

            ImGui::PushID(i);
            ImGui::TextColored(color, "[%s] [%s:%d] %s",
                               catName, m.file.c_str(), m.line, m.text.c_str());

            // 行のダブルクリックで Visual Studio を開く
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

    // 一番下を見ているときだけ、新しい行に合わせてスクロールする
    if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}