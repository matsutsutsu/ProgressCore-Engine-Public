#include "Editor/Windows/AI/NavMeshWindow.h"
#include <imgui.h>

NavMeshWindow::NavMeshWindow() : EditorWindow("NavMesh Builder") {}

void NavMeshWindow::DrawContents(EditorContext &context)
{
    // World が無い（読み込み前など）ときは何もしない
    if (!context.world) return;

    ImGui::SeparatorText("Agent Settings");

    // ---- エージェントの大きさと移動能力 ----
    ImGui::DragFloat("Agent Radius (太さ)", &m_navSettings.agentRadius, 0.05f, 0.1f, 5.0f);
    ImGui::DragFloat("Agent Height (身長)", &m_navSettings.agentHeight, 0.1f, 0.1f, 5.0f);
    ImGui::DragFloat("Max Climb (段差)", &m_navSettings.agentMaxClimb, 0.05f, 0.0f, 2.0f);
    ImGui::DragFloat("Max Slope (斜面角度)", &m_navSettings.agentMaxSlope, 1.0f, 0.0f, 90.0f);

    ImGui::Spacing();

    // ---- ボクセルの解像度など。普段は閉じておき、精度を詰めたいときだけ開く ----
    if (ImGui::CollapsingHeader("Advanced Settings"))
    {
        ImGui::DragFloat("Cell Size (水平の解像度)", &m_navSettings.cellSize, 0.05f, 0.1f, 1.0f);
        ImGui::DragFloat("Cell Height (高さの解像度)", &m_navSettings.cellHeight, 0.05f, 0.1f, 1.0f);
        ImGui::DragFloat("Region Min Size", &m_navSettings.regionMinSize, 1.0f, 0.0f, 50.0f);
        ImGui::DragFloat("Region Merge Size", &m_navSettings.regionMergeSize, 1.0f, 0.0f, 50.0f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- Bake の実行。ボタンは緑にして目立たせる ----
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));

    if (ImGui::Button("Bake NavMesh", ImVec2(-1, 40)))
    {
        // 前回の NavMesh は BuildNavMesh の中で解放してから差し替えるので、何度押してもよい
        NavMeshBuilder::BuildNavMesh(*context.world, m_navSettings);
    }

    ImGui::PopStyleColor(3);
}