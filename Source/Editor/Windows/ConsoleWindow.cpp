#include "ConsoleWindow.h"
#include "Engine/Platform/Logger.h"
#include <imgui.h>

ConsoleWindow::ConsoleWindow() : EditorWindow("Console") {}

void ConsoleWindow::DrawContents(EditorContext& context)
{
    // =======================================================
    // 1. ツールバー (クリアボタン、フィルター、自動スクロール)
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

    ImGui::Separator();

    // =======================================================
    // 2. ログの描画 (Clipperを使った超高速描画)
    // =======================================================
    const auto& messages = Logger::Instance().GetMessages();

    // フィルターに合致するログだけを抽出してインデックスを記録
    std::vector<int> filteredIndices;
    filteredIndices.reserve(messages.size());
    for (int i = 0; i < (int)messages.size(); ++i) {
        auto lvl = messages[i].level;
        if (lvl == LogLevel::Info && !_showInfo) continue;
        if (lvl == LogLevel::Warning && !_showWarning) continue;
        if (lvl == LogLevel::Error && !_showError) continue;
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
            ImVec4 color = ImVec4(1, 1, 1, 1); // 白(Info)
            if (msg.level == LogLevel::Warning) color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // 黄色
            if (msg.level == LogLevel::Error)   color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // 赤

            ImGui::TextColored(color, "%s", msg.text.c_str());
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