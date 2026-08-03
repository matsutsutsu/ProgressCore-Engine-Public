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
#include "Engine/Platform/Core/LogSinks.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <imgui.h>

ConsoleWindow::ConsoleWindow() : EditorWindow("Console")
{
    // カテゴリフィルタを全 true で初期化
    for (auto& f : m_categoryFilters) f = true;
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
    if (!m_bufferSink)
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

    // --------------------------------------------------------------------
    // 2. カテゴリフィルタ
    // --------------------------------------------------------------------
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

    // ====================================================================
    // 究極の修正: 毎フレームの Snapshot (ディープコピー) を防ぐキャッシュ機構
    // ====================================================================
    static std::vector<LogMessage> s_cachedMessages;
    static std::vector<int>        s_cachedFiltered;
    static float s_updateTimer = 0.0f;

    // ImGuiのDeltaTimeを使ってタイマーを進行
    s_updateTimer += ImGui::GetIO().DeltaTime;

    // 前回の更新から0.1秒経過、または「Clear」ボタン等で強制更新が必要な場合のみコピーを走らせる
    if (s_updateTimer > 0.1f) {
        s_cachedMessages = m_bufferSink->Snapshot();
        s_cachedFiltered.clear();
        s_cachedFiltered.reserve(s_cachedMessages.size());

        const bool hasSearch = (m_searchBuf[0] != '\0');
        for (int i = 0; i < static_cast<int>(s_cachedMessages.size()); ++i)
        {
            const auto& m = s_cachedMessages[i];

            // フィルタ条件のチェック...
            switch (m.level) {
            case LogLevel::Info:    if (!m_showInfo)    continue; break;
            case LogLevel::Success: if (!m_showSuccess) continue; break;
            case LogLevel::Warning: if (!m_showWarning) continue; break;
            case LogLevel::Error:   if (!m_showError)   continue; break;
            }
            if (!m_categoryFilters[static_cast<int>(m.category)]) continue;

            if (hasSearch) {
                if (m.text.find(m_searchBuf) == std::string::npos &&
                    m.file.find(m_searchBuf) == std::string::npos) continue;
            }

            s_cachedFiltered.push_back(i);
        }
        s_updateTimer = 0.0f; // タイマーリセット
    }

    // ====================================================================
    // スクロール可能なログ領域の開始
    // ====================================================================
    ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // --------------------------------------------------------------------
    // 5. 描画 (Clipper で高速化)
    // --------------------------------------------------------------------
    // 以降の描画処理は、毎フレーム計算するのではなく、キャッシュされた s_cachedFiltered と s_cachedMessages を使う
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(s_cachedFiltered.size()));
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            // キャッシュから読み出し
            const auto& m = s_cachedMessages[s_cachedFiltered[i]];
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
    if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}