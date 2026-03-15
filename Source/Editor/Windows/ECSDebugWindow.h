#pragma once
#include "Editor/Core/EditorWindow.h"
#include "ECS/Core/CCL_World.h"
#include "ECS/Core/CCL_Chunk.h"
#include "ECS/System/CCL_SystemManager.h"
#include "ECS/Core/CCL_PendingOps.h"
#include <imgui.h>
#include <implot.h>
#include <vector>
#include <string>

class ECSDebugWindow : public EditorWindow {
public:
    ECSDebugWindow() : EditorWindow("ECS Debugger") {}

protected:
    void DrawContents(EditorContext& context) override {
        if (!context.world || !context.systemManager) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "World or SystemManager is not initialized.");
            return;
        }

        if (ImGui::BeginTabBar("ECSDebugTabs")) {

            // 1. 実際のキャパシティに基づいたメモリ状況
            if (ImGui::BeginTabItem("Memory & Chunks")) {
                DrawMemoryTab(context.world);
                ImGui::EndTabItem();
            }

            // 2. システム負荷
            if (ImGui::BeginTabItem("Systems Profiler")) {
                DrawSystemsTab(context.systemManager);
                ImGui::EndTabItem();
            }

            // 3. 待機操作の可視化
            if (ImGui::BeginTabItem("Pending Operations")) {
                DrawPendingOpsTab(context.world);
                ImGui::EndTabItem();
            }



            ImGui::EndTabBar();
        }
    }

private:
    // ユーティリティ関数（デバッグウィンドウ外またはクラス内に追加）
    const char* GetFriendlyTypeName(uint32_t id) {
        // 本来はコンポーネント登録時に名前を保存しておくべきですが、
        // 暫定的にIDを表示。拡張して std::unordered_map<uint32_t, string> 等から引くのが理想。
        static char buf[32];
        sprintf_s(buf, "Type:%u", id);
        return buf;
    }

    void DrawMemoryTab(CCL::ECS::Core::World* world) {
        auto& chunks = world->GetChunkManager().GetChunks();
        static int selectedChunkIdx = -1;
        static EntityID selectedEntityID = 0;

        ImGui::Columns(2, "MemoryTabSplitter", true);
        ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.40f);

        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Chunk Hierarchy");
        if (ImGui::Button("Refresh Layout")) { /* 必要なら再計算 */ }
        ImGui::Separator();

        if (ImGui::BeginChild("ChunkList")) {
            for (size_t i = 0; i < chunks.size(); ++i) {
                auto& chunk = chunks[i];
                size_t count = chunk->GetEntityCount();
                size_t cap = chunk->GetCapacity();

                ImGui::PushID((int)i);
                bool isSelected = (selectedChunkIdx == (int)i);

                // チャンクヘッダー
                char label[128];
                sprintf_s(label, "Chunk %zu [%zu/%zu]", i, count, cap);
                if (ImGui::Selectable(label, isSelected)) {
                    selectedChunkIdx = (int)i;
                }

                // メモリ密度ドットマップ
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                float width = ImGui::GetContentRegionAvail().x;
                float height = 14.0f;
                float boxWidth = width / (cap > 0 ? cap : 1);

                drawList->AddRectFilled(p, ImVec2(p.x + width, p.y + height), IM_COL32(30, 30, 30, 255), 2.0f);

                for (size_t e = 0; e < count; ++e) {
                    // エンティティの状態に応じて色を変える（例：削除待ちはオレンジ、生存は緑）
                    ImU32 color = IM_COL32(0, 200, 100, 255);
                    drawList->AddRectFilled(
                        ImVec2(p.x + e * boxWidth + 1, p.y + 1),
                        ImVec2(p.x + (e + 1) * boxWidth - 1, p.y + height - 1),
                        color
                    );
                }

                ImGui::Dummy(ImVec2(width, height + 6));

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Archetype Hash: %llu", chunk->GetArchetypeHash());
                    ImGui::Separator();
                    for (const auto& td : chunk->GetTypeDatas()) {
                        ImGui::BulletText("%s (Size: %zu)", GetFriendlyTypeName(td.id), td.typeSize);
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        // --- 右カラム: エンティティ・コンポーネント詳細 ---
        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Entity Inspector");
        ImGui::Separator();

        if (selectedChunkIdx >= 0 && selectedChunkIdx < (int)chunks.size()) {
            auto& chunk = chunks[selectedChunkIdx];

            if (ImGui::BeginChild("EntityDetailList")) {
                ImGui::Text("Chunk [%d] Archetype", selectedChunkIdx);

                for (size_t e = 0; e < chunk->GetEntityCount(); ++e) {
                    EntityID id = chunk->GetEntityByIndex(e);
                    char eLabel[64];
                    sprintf_s(eLabel, "Slot %zu: Entity ID %u", e, id);

                    if (ImGui::TreeNode(eLabel)) {
                        for (const auto& td : chunk->GetTypeDatas()) {
                            if (ImGui::TreeNode((void*)(intptr_t)td.id, "Component: %s", GetFriendlyTypeName(td.id))) {
                                // メモリの生データ可視化（非常に重要！）
                                void* data = chunk->GetComponentPtrByType(td.id, e);
                                if (data) {
                                    // とりあえず最初の4バイト分をfloatとintで表示（デバッグ用）
                                    float* fv = (float*)data;
                                    int* iv = (int*)data;
                                    ImGui::Text("Raw(f): %.3f, %.3f, %.3f", fv[0], fv[1], fv[2]);
                                    ImGui::Text("Raw(i): %d, %d, %d", iv[0], iv[1], iv[2]);

                                    if (ImGui::Button("Edit Data")) {
                                        // 将来的にはここで専用のUIを出す
                                    }
                                }
                                ImGui::TreePop();
                            }
                        }
                        ImGui::TreePop();
                    }
                }
            }
            ImGui::EndChild();
        }
        else {
            ImGui::TextDisabled("Select a chunk to see details.");
        }
        ImGui::Columns(1);
    }

    void DrawSystemsTab(CCL::ECS::SystemManager *manager)
    {

        // 1. LogicとRenderの両方のリストを取得
        const auto &logicSystems  = manager->GetLogicSystems();
        const auto &renderSystems = manager->GetRenderSystems();

        // 2. 表示用に1つのリストにまとめる（生ポインタの配列を作る）
        std::vector<CCL::ECS::SystemBase*> systems;
        systems.reserve(logicSystems.size() + renderSystems.size());

        for (const auto& s : logicSystems) systems.push_back(s.get());
        for (const auto& s : renderSystems) systems.push_back(s.get());


        // 1. 合計時間の算出
        float totalMs = 0.0f;
        for (auto& sys : systems) {
            totalMs += sys->GetDisplayUpdateTime();
        }

        // フレームレート警告表示
        ImGui::Text("Total ECS Frame Time: ");
        ImGui::SameLine();
        ImVec4 statusColor = (totalMs > 16.66f) ? ImVec4(1, 0.4f, 0.4f, 1) : ImVec4(0.4f, 1, 0.4f, 1);
        ImGui::TextColored(statusColor, "%.3f ms (%.1f FPS)", totalMs, (totalMs > 0) ? 1000.0f / totalMs : 0);

        ImGui::Separator();

        // --- 戦略的改良: スタック型 Workload Distribution Bar ---
        ImGui::Text("Workload Distribution:");

        // 描画領域の確保
        ImVec2 barSize = ImVec2(ImGui::GetContentRegionAvail().x, 30.0f);
        ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 背景（空の時のベース）
        drawList->AddRectFilled(screenPos, ImVec2(screenPos.x + barSize.x, screenPos.y + barSize.y), IM_COL32(45, 45, 48, 255), 4.0f);

        float currentX = 0.0f;
        if (totalMs > 0.0f) {
            for (size_t i = 0; i < systems.size(); ++i) {
                auto& sys = systems[i];
                float ms = sys->GetDisplayUpdateTime();
                float ratio = ms / totalMs;

                if (ratio < 0.001f) continue; // 0.1%未満は描画スキップ

                float segmentWidth = ratio * barSize.x;
                ImVec2 pMin = ImVec2(screenPos.x + currentX, screenPos.y);
                ImVec2 pMax = ImVec2(screenPos.x + currentX + segmentWidth, screenPos.y + barSize.y);

                // システムごとの色分け（名前からハッシュで生成し、彩度を調整）
                ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(
                    (float)((i * 130) % 255) / 255.0f,
                    (float)((i * 200) % 255) / 255.0f,
                    (float)((i * 150) % 255) / 255.0f,
                    1.0f));

                // 実行時間が長い（例えば1ms超え）場合は警告として少し明度を上げる
                if (ms > 1.0f) col = ImColor(255, 140, 0, 255);

                drawList->AddRectFilled(pMin, pMax, col, (i == 0 || i == systems.size() - 1) ? 4.0f : 0.0f);

                // ホバー判定と詳細ツールチップ
                if (ImGui::IsMouseHoveringRect(pMin, pMax)) {
                    drawList->AddRect(pMin, pMax, IM_COL32(255, 255, 255, 200), 0, 0, 2.0f); // 強調枠
                    ImGui::BeginTooltip();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[System: %s]", sys->GetName().c_str());
                    ImGui::Text("Time : %.4f ms", ms);
                    ImGui::Text("Ratio: %.1f %%", ratio * 100.0f);
                    ImGui::EndTooltip();
                }

                currentX += segmentWidth;
            }
        }

        // ImGuiのカーソルをバーの分だけ進める（これを忘れると重なる）
        ImGui::Dummy(barSize);
        ImGui::Spacing();

        // --- 推移グラフ (ImPlot) ---
        if (ImPlot::BeginPlot("System Load Profile", ImVec2(-1, 200), ImPlotFlags_NoMouseText)) {
            ImPlot::SetupAxes("Systems", "ms");
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, (totalMs > 1.0f) ? totalMs * 1.1f : 2.0f, ImGuiCond_Always);

            for (auto& sys : systems) {
                float val = sys->GetDisplayUpdateTime();
                ImPlot::PlotBars(sys->GetName().c_str(), &val, 1);
            }
            ImPlot::EndPlot();
        }

        // --- 詳細データテーブル ---
        if (ImGui::BeginTable("SystemsDetailTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("System Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Load Bar", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();

            for (auto& sys : systems) {
                float ms = sys->GetDisplayUpdateTime();
                float ratio = (totalMs > 0) ? (ms / totalMs) : 0;

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", sys->GetName().c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f", ms);

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.1f%%", ratio * 100.0f);

                ImGui::TableSetColumnIndex(3);
                // 負荷に応じた動的なバー（赤～緑）
                ImVec4 barColor = ImVec4(ratio, 1.0f - ratio, 0.3f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
                ImGui::ProgressBar(ratio, ImVec2(-1, 14), "");
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
    }

    void DrawPendingOpsTab(CCL::ECS::Core::World* world) {
        auto& pending = world->GetPendingOps();
        auto& ops = pending.Ops();

        ImGui::Text("Queue Size: %zu", ops.size());
        if (ImGui::Button("Sync Now (ScrutinyAndApply)")) {
            world->ScrutinyAndApply();
        }
        ImGui::Separator();

        ImGui::BeginChild("OpsLog", ImVec2(0, 0), true);
        for (const auto& op : ops) {
            const char* typeName = "Unknown";
            ImVec4 color = ImVec4(1, 1, 1, 1);
            switch (op.kind) {
            case CCL::ECS::Core::PendingOpKind::Spawn:           typeName = "SPAWN";   color = ImVec4(0, 1, 0, 1); break;
            case CCL::ECS::Core::PendingOpKind::Destroy:         typeName = "DESTROY"; color = ImVec4(1, 0, 0, 1); break;
            case CCL::ECS::Core::PendingOpKind::AddComponent:    typeName = "ADD";     color = ImVec4(0, 0.7f, 1, 1); break;
            case CCL::ECS::Core::PendingOpKind::RemoveComponent: typeName = "REMOVE";  color = ImVec4(1, 0.5f, 0, 1); break;
            }
            ImGui::TextColored(color, "[%s]", typeName);
            ImGui::SameLine();
            ImGui::Text("EntityID: %u | TypeID: %u", op.entity, op.type);
        }
        ImGui::EndChild();
    }
};