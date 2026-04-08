#include "ConsoleWindow.h"
#include "Engine/Platform/Logger.h"
#include <imgui.h>

ConsoleWindow::ConsoleWindow() : EditorWindow("Console") {}

void ConsoleWindow::DrawContents(EditorContext& context)
{
    // =======================================================
    // 1. ツールバー (クリアボタン、レベルフィルター、自動スクロール)
    // =======================================================
    if (ImGui::Button("Clear")) {
        Logger::Instance().Clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-Scroll", &_autoScroll);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &_showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warning", &_showWarning);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &_showError);

    // =======================================================
    // ★ 追加: カテゴリのフィルター描画 (横に並べる)
    // =======================================================
    ImGui::Separator();
    ImGui::Text("Categories: ");
    ImGui::SameLine();

    // カテゴリ名を取得するためのヘルパー配列
    const char* catNames[] = { "Core", "ECS", "Physics", "Render", "Audio", "Game" };

    for (int i = 0; i < (int)LogCategory::Count; ++i) {
        ImGui::Checkbox(catNames[i], &_categoryFilters[i]);
        if (i < (int)LogCategory::Count - 1) ImGui::SameLine();
    }
    ImGui::Separator();

    // =======================================================
    // 2. ログの描画 (Clipperを使った超高速描画)
    // =======================================================
    const auto& messages = Logger::Instance().GetMessages();

    // フィルターに合致するログだけを抽出してインデックスを記録
    std::vector<int> filteredIndices;
    filteredIndices.reserve(messages.size());

    for (int i = 0; i < (int)messages.size(); ++i) {
        const auto& msg = messages[i];

        // レベルのフィルター
        if (msg.level == LogLevel::Info && !_showInfo) continue;
        if (msg.level == LogLevel::Warning && !_showWarning) continue;
        if (msg.level == LogLevel::Error && !_showError) continue;

        // ★ 追加: カテゴリのフィルター（チェックが外れていたらスキップ）
        if (!_categoryFilters[(int)msg.category]) continue;

        filteredIndices.push_back(i);
    }

    // スクロール可能な子ウィンドウ開始
    ImGui::BeginChild("LogRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // 画面に映っている行だけを描画する魔法
    ImGuiListClipper clipper;
    clipper.Begin((int)filteredIndices.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& msg = messages[filteredIndices[i]];

            // レベルに応じて色を変える
            ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // 白(Info)
            if (msg.level == LogLevel::Warning) color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // 黄色
            if (msg.level == LogLevel::Error)   color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // 赤

            // ★ 修正: カテゴリ名、ファイル名、行数も合わせて綺麗にフォーマットする
            const char* catName = catNames[(int)msg.category];
            ImGui::TextColored(color, "[%s] [%s:%d] %s", catName, msg.file.c_str(), msg.line, msg.text.c_str());
        }
    }
    clipper.End();

    // =======================================================
    // 3. 自動スクロール処理
    // =======================================================
    // 新しいログが追加され、かつ一番下までスクロールされていたら追従する
    if (_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}