#include "Editor/Windows/General/MemoryAnalyzerWindow.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include <imgui.h>
#include <algorithm>

MemoryAnalyzerWindow::MemoryAnalyzerWindow()
    : EditorWindow("Memory Analyzer"), m_cachedTotalVram(0), m_updateTimer(0.2f) // 初回の描画ですぐ走査する
{
}



void MemoryAnalyzerWindow::DrawContents(EditorContext &context)
{
    // 0.2 秒（60fps で約12フレーム）に1回だけプールを走査する
    m_updateTimer += ImGui::GetIO().DeltaTime;
    if (m_updateTimer >= 0.2f)
    {
        m_updateTimer = 0.0f;
        m_cachedTextures.clear();
        m_cachedTotalVram = 0;

        auto &rm = ResourceManager::Instance();

        // ResourceManager のテクスチャプールを走査する
        rm.GetTexturePool().ForEachActive([&](const TextureResource &tex) {
            CachedAssetInfo info;
            info.path      = tex.debugPath;
            info.sizeBytes = tex.byteSize;
            info.width     = tex.width;
            info.height    = tex.height;

            m_cachedTextures.push_back(info);
            m_cachedTotalVram += tex.byteSize;
        });

        // VRAM の使用量が多い順に並べる
        std::sort(m_cachedTextures.begin(), m_cachedTextures.end(), [](const CachedAssetInfo &a, const CachedAssetInfo &b) {
            return a.sizeBytes > b.sizeBytes;
        });
    }

    // 描画はキャッシュした結果から行う
    DrawOverviewSection();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawTextureTableSection();
}



void MemoryAnalyzerWindow::DrawOverviewSection()
{
    float vramMb   = static_cast<float>(m_cachedTotalVram) / (1024.0f * 1024.0f);
    float budgetMb = static_cast<float>(VRAM_BUDGET_BYTES) / (1024.0f * 1024.0f);
    float ratio    = static_cast<float>(m_cachedTotalVram) / VRAM_BUDGET_BYTES;

    ImGui::Text("VRAM Budget Status:");
    ImGui::SameLine();

    // 予算の 85% を超えたら文字とバーを赤くして警告する
    ImVec4 textColor = (ratio > 0.85f) ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    ImGui::TextColored(textColor, "%.1f MB / %.0f MB (%.1f%%)", vramMb, budgetMb, ratio * 100.0f);

    if (ratio > 0.85f)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    else
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));

    ImGui::ProgressBar(ratio, ImVec2(-1, 25), "");
    ImGui::PopStyleColor();
}



void MemoryAnalyzerWindow::DrawTextureTableSection()
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Loaded Textures Breakdown (Sorted by Size)");

    if (ImGui::BeginTable("TextureMemoryTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 0)))
    {
        ImGui::TableSetupColumn("Asset Virtual Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("VRAM Size", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();

        for (const auto &tex : m_cachedTextures)
        {
            ImGui::TableNextRow();

            // パス
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tex.path.c_str());

            // 解像度（例: 2048 x 2048）
            ImGui::TableNextColumn();
            ImGui::Text("%u x %u", tex.width, tex.height);

            // 使用量（1MB 以上は MB、それ未満は KB）
            ImGui::TableNextColumn();
            if (tex.sizeBytes >= 1024 * 1024)
            {
                ImGui::Text("%.2f MB", static_cast<float>(tex.sizeBytes) / (1024.0f * 1024.0f));
            }
            else
            {
                ImGui::Text("%.1f KB", static_cast<float>(tex.sizeBytes) / 1024.0f);
            }
        }
        ImGui::EndTable();
    }
}