#include "ECSDebugWindow.h"
#include <implot.h>
#include "ECS/Core/CCL_Chunk.h"
#include "ECS/Core/CCL_PendingOps.h"

// ---------------------------------------------------------
// ScrollingBuffer の実装
// ---------------------------------------------------------
ScrollingBuffer::ScrollingBuffer(int max_size) {
    MaxSize = max_size;
    Offset = 0;
    Data.reserve(MaxSize);
}

void ScrollingBuffer::AddPoint(float x, float y) {
    if (Data.size() < MaxSize)
        Data.push_back(ImVec2(x, y));
    else {
        Data[Offset] = ImVec2(x, y);
        Offset = (Offset + 1) % MaxSize;
    }
}

// ---------------------------------------------------------
// ECSDebugWindow の実装
// ---------------------------------------------------------
ECSDebugWindow::ECSDebugWindow() : EditorWindow("ECS Debugger") {}

void ECSDebugWindow::DrawContents(EditorContext& context) {
    if (!context.world || !context.systemManager) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "World or SystemManager is not initialized.");
        return;
    }

    if (ImGui::BeginTabBar("ECSDebugTabs")) {
        if (ImGui::BeginTabItem("Memory & Chunks")) {
            DrawMemoryTab(context.world);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Systems Profiler")) {
            DrawSystemsTab(context.systemManager);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Pending Ops")) {
            DrawPendingOpsTab(context.world);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void ECSDebugWindow::DrawSystemsTab(CCL::ECS::SystemManager* systemManager) {
    float dt = ImGui::GetIO().DeltaTime;

    // =======================================================
    // 1. ツールバー (UXの劇的改善)
    // =======================================================
    ImGui::Checkbox("Pause Profiler", &_pauseProfiler);
    ImGui::SameLine();

    // ★ UX改善 1: 一括ON/OFFボタン
    if (ImGui::Button("Hide All")) {
        for (auto& pair : _systemTimings) pair.second.isVisible = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Show All")) {
        for (auto& pair : _systemTimings) pair.second.isVisible = true;
    }

    // ★ UX改善 2: グラフのY軸上限を手動で固定するスライダー
    // これにより、グラフが勝手に伸び縮みして目が回るのを防ぎます
    static float yAxisMax = 16.66f; // デフォルトは60fps(16.6ms)のライン
    ImGui::SameLine(0, 20);
    ImGui::SetNextItemWidth(150);
    ImGui::SliderFloat("Y-Axis Max (ms)", &yAxisMax, 1.0f, 60.0f, "%.1f");


    // データの記録用ヘルパー (前回と同じ)
    auto addData = [&](const std::string& name, float timeMs) {
        auto& data = _systemTimings[name];
        if (data.buffer.Data.empty()) {
            float hue = fmod(_systemTimings.size() * 0.618033988749895f, 1.0f);
            float r, g, b;
            ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 0.9f, r, g, b);
            data.color = ImVec4(r, g, b, 1.0f);
        }
        data.buffer.AddPoint(_time, timeMs);
        };

    // 2. データの記録 (前回と同じ)
    if (!_pauseProfiler) {
        _time += dt;
        for (const auto& sys : systemManager->GetLogicSystems()) {
            addData(sys->GetName(), sys->GetLastUpdateTime());
        }
        for (const auto& sys : systemManager->GetRenderSystems()) {
            addData(sys->GetName(), sys->GetLastUpdateTime());
        }
    }

    // =======================================================
    // 3. ImPlot による折れ線グラフの描画
    // =======================================================
    if (ImPlot::BeginPlot("System Execution Times (ms)", ImVec2(-1, 250))) {

        // ★ UX改善 2の適用: Y軸の最大値をスライダーの値で「完全に固定」する
        ImPlot::SetupAxes("Time (s)", "Execution Time (ms)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_X1, _time - 5.0, _time, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, yAxisMax, ImGuiCond_Always); // ★ Y軸を固定

        for (auto& pair : _systemTimings) {
            if (!pair.second.isVisible || pair.second.buffer.Data.empty()) continue;

            float thickness = pair.second.isHovered ? 3.0f : 1.0f;
            ImPlot::SetNextLineStyle(pair.second.color, thickness);
            ImPlot::PlotLine(pair.first.c_str(),
                &pair.second.buffer.Data[0].x,
                &pair.second.buffer.Data[0].y,
                pair.second.buffer.Data.size(),
                0,
                pair.second.buffer.Offset,
                sizeof(ImVec2));
        }
        ImPlot::EndPlot();
    }

    ImGui::Separator();

    // =======================================================
    // 4. 詳細テーブル
    // =======================================================
    ImGui::Text("Current Performance Breakdown");
    if (ImGui::BeginTable("SystemPerfTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY, ImVec2(0, 0))) {
        ImGui::TableSetupColumn("System Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Latest Time (ms)", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Load Bar", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        for (auto& pair : _systemTimings) {
            if (pair.second.buffer.Data.empty()) continue;

            int latestIdx = 0;
            if (pair.second.buffer.Data.size() < pair.second.buffer.MaxSize) {
                latestIdx = pair.second.buffer.Data.size() - 1;
            }
            else {
                latestIdx = (pair.second.buffer.Offset == 0) ? pair.second.buffer.MaxSize - 1 : pair.second.buffer.Offset - 1;
            }
            float latestTime = pair.second.buffer.Data[latestIdx].y;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImGui::PushID(pair.first.c_str());
            ImGui::Checkbox("##vis", &pair.second.isVisible);
            ImGui::SameLine();

            ImGui::ColorButton("##col", pair.second.color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(14, 14));
            ImGui::SameLine();

            // ★ UX改善 4: Selectableの引数を修正し、クリック時の違和感（点滅や入力奪取）を消滅させました
            ImGui::Selectable(pair.first.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
            pair.second.isHovered = ImGui::IsItemHovered();

            ImGui::PopID();

            ImGui::TableNextColumn();
            ImGui::Text("%.3f ms", latestTime);

            ImGui::TableNextColumn();
            float ratio = latestTime / 16.666f;
            ImVec4 barColor = (ratio > 0.5f) ? ImVec4(1, 0, 0, 1) : ImVec4(0, 1, 0, 1);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            ImGui::ProgressBar(ratio, ImVec2(-1, 14), "");
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
}


void ECSDebugWindow::DrawMemoryTab(CCL::ECS::Core::World* world) {
    // 既存のメモリ描画ロジックをここに移動してください
    ImGui::Text("Memory Tab (Implemented in previous version)");
}

void ECSDebugWindow::DrawPendingOpsTab(CCL::ECS::Core::World* world) {
    // 既存の PendingOps 描画ロジックをここに移動してください
    ImGui::Text("Pending Ops Tab (Implemented in previous version)");
}