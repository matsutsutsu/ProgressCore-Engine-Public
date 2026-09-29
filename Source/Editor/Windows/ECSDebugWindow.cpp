#include "ECSDebugWindow.h"
#include "Engine/ECS/Core/CCL_Chunk.h"

#include "Editor/Inspector/ComponentGuiRegistry.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

ECSDebugWindow::ECSDebugWindow() : EditorWindow("ECS Debugger") {}

void ECSDebugWindow::DrawContents(EditorContext &context)
{
    if (!context.world || !context.systemManager)
    {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "World or SystemManager is not initialized.");
        return;
    }

    if (ImGui::BeginTabBar("ECSDebugTabs"))
    {
        if (ImGui::BeginTabItem("Memory & Chunks"))
        {
            DrawMemoryTab(context.world);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Execution Graph"))
        {
            DrawExecutionGraphTab(context.systemManager);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

const char *ECSDebugWindow::GetFriendlyTypeName(uint32_t id)
{
    const std::string &name = ComponentGuiRegistry::Instance().GetComponentName(id);
    if (!name.empty())
    {
        return name.c_str();
    }

    // GUI レジストリに登録されていない型は ID で表示する
    static char buf[64];
    snprintf(buf, sizeof(buf), "Unknown (ID:%u)", id);
    return buf;
}

// data から size バイトを、1行16バイトの16進数と ASCII で表示する
static void DrawHexDump(const void *data, size_t size)
{
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    char                 lineBuffer[128];

    // 1行あたり16バイトずつ描画
    for (size_t i = 0; i < size; i += 16)
    {
        int offset = snprintf(lineBuffer, sizeof(lineBuffer), "%04zX: ", i);

        for (size_t j = 0; j < 16; ++j)
        {
            if (i + j < size)
            {
                offset += snprintf(lineBuffer + offset, sizeof(lineBuffer) - offset, "%02X ", bytes[i + j]);
            }
            else
            {
                offset += snprintf(lineBuffer + offset, sizeof(lineBuffer) - offset, "   ");
            }
        }

        // 右側に ASCII。表示できない文字は '.' にする
        offset += snprintf(lineBuffer + offset, sizeof(lineBuffer) - offset, " | ");
        for (size_t j = 0; j < 16 && i + j < size; ++j)
        {
            char c = bytes[i + j];
            lineBuffer[offset++] = (c >= 32 && c < 127) ? c : '.';
        }
        lineBuffer[offset] = '\0';

        ImGui::TextDisabled("%s", lineBuffer);
    }
}

// ============================================================
// Memory & Chunks（チャンクの使用状況と、エンティティの中身）
// ============================================================
void ECSDebugWindow::DrawMemoryTab(CCL::ECS::World *world)
{
    auto &chunks = world->GetChunkManager().GetChunks();

    // ---- 全チャンクの合計 ----
    size_t totalEntities = 0;
    size_t totalCapacity = 0;
    size_t totalMemory   = 0;

    for (const auto &chunk : chunks)
    {
        if (!chunk) continue;
        totalEntities += chunk->GetEntityCount();
        totalCapacity += chunk->GetCapacity();
        totalMemory += chunk->GetChunkSize() * chunk->GetCapacity();
    }

    ImGui::Text("Active Chunks: %zu", chunks.size());
    ImGui::SameLine(200);
    ImGui::Text("Total Entities: %zu / %zu", totalEntities, totalCapacity);
    ImGui::SameLine(450);
    ImGui::Text("Est. Component Memory: %.3f MB", (float)totalMemory / (1024.0f * 1024.0f));
    ImGui::Spacing();

    static int selectedChunkIdx = -1; // 右カラムに表示するチャンク

    ImGui::Columns(2, "MemoryTabSplitter", true);
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.40f);

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Chunk Hierarchy by Archetype");
    ImGui::Separator();

    // --- 左カラム: アーキタイプごとにグループ化されたチャンクリスト ---
    if (ImGui::BeginChild("ChunkList"))
    {
        // チャンクをアーキタイプのハッシュごとにまとめる
        std::map<uint64_t, std::vector<int>> chunksByArchetype;
        for (size_t i = 0; i < chunks.size(); ++i)
        {
            if (chunks[i]) chunksByArchetype[chunks[i]->GetArchetypeHash()].push_back((int)i);
        }

        // 分類したアーキタイプごとにツリーを描画
        for (const auto &pair : chunksByArchetype)
        {
            uint64_t    hash         = pair.first;
            const auto &chunkIndices = pair.second;

            // アーキタイプを構成するコンポーネント名を連結する
            std::string componentNames = "";
            if (!chunkIndices.empty() && chunks[chunkIndices[0]])
            {
                auto &firstChunk = chunks[chunkIndices[0]];
                for (const auto &td : firstChunk->GetTypeDatas())
                {
                    if (!componentNames.empty()) componentNames += ", ";
                    componentNames += GetFriendlyTypeName(td.id);
                }
            }

            // 例: [TransformComponent, VelocityComponent] (3 Chunks)
            // 同じ表示名が並んでも ID が重ならないよう、末尾に ##ハッシュ値 を付ける
            char archLabel[1024];
            snprintf(archLabel, sizeof(archLabel), "[%s] (%zu Chunks)##%llu", componentNames.c_str(), chunkIndices.size(), hash);

            if (ImGui::TreeNode(archLabel))
            {
                for (int idx : chunkIndices)
                {
                    auto  &chunk = chunks[idx];
                    size_t count = chunk->GetEntityCount();
                    size_t cap   = chunk->GetCapacity();

                    ImGui::PushID(idx);
                    bool isSelected = (selectedChunkIdx == idx);

                    char label[128];
                    snprintf(label, sizeof(label), "Chunk %d [%zu/%zu]", idx, count, cap);
                    if (ImGui::Selectable(label, isSelected))
                    {
                        selectedChunkIdx = idx;
                    }

                    // 使用状況のバー。1マスが1スロットで、緑が使用中、赤が削除済み
                    ImDrawList *drawList = ImGui::GetWindowDrawList();
                    ImVec2      p        = ImGui::GetCursorScreenPos();
                    float       width    = ImGui::GetContentRegionAvail().x;
                    float       height   = 10.0f;
                    float       boxWidth = width / (cap > 0 ? cap : 1);

                    drawList->AddRectFilled(p, ImVec2(p.x + width, p.y + height), IM_COL32(30, 30, 30, 255), 2.0f);

                    const bool hasDestroyed = chunk->HasDestroyedEntities();
                    for (size_t e = 0; e < count; ++e)
                    {
                        ImU32 color = IM_COL32(0, 200, 100, 255);
                        if (hasDestroyed && chunk->IsEntityDestroyed(e))
                        {
                            color = IM_COL32(200, 50, 50, 255);
                        }
                        drawList->AddRectFilled(
                            ImVec2(p.x + e * boxWidth, p.y),
                            ImVec2(p.x + (e + 1) * boxWidth - 0.5f, p.y + height),
                            color);
                    }
                    ImGui::Dummy(ImVec2(width, height + 4));
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::EndChild();

    ImGui::NextColumn();

    // --- 右カラム: 選択中チャンクのエンティティ詳細 ---
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Memory Inspector (Hex Dump)");
    ImGui::Separator();

    if (selectedChunkIdx >= 0 && selectedChunkIdx < (int)chunks.size())
    {
        auto &chunk = chunks[selectedChunkIdx];
        if (chunk)
        {
            if (ImGui::BeginChild("EntityDetailList"))
            {
                // チャンクに含まれるコンポーネントの種類
                ImGui::Text("Components in this chunk:");
                for (const auto &td : chunk->GetTypeDatas())
                {
                    ImGui::BulletText("%s (%zu bytes)", GetFriendlyTypeName(td.id), td.typeSize);
                }
                ImGui::Separator();
                ImGui::Spacing();

                for (size_t e = 0; e < chunk->GetEntityCount(); ++e)
                {
                    // 削除済みのスロットは出さない
                    if (chunk->HasDestroyedEntities() && chunk->IsEntityDestroyed(e)) continue;

                    CCL::ECS::EntityID id = chunk->GetEntityByIndex(e);
                    char               eLabel[64];
                    snprintf(eLabel, sizeof(eLabel), "Slot %zu: Entity ID %llu", e, id);

                    if (ImGui::TreeNode(eLabel))
                    {
                        for (const auto &td : chunk->GetTypeDatas())
                        {
                            if (ImGui::TreeNode((void *)(intptr_t)td.id, "Component: %s", GetFriendlyTypeName(td.id)))
                            {
                                void *data = chunk->GetComponentPtrByType(td.id, e);
                                if (data)
                                {
                                    // 値の表示と編集は GUI レジストリに任せる。
                                    // 型ごとのウィジェット（Transform なら Position の x/y/z スライダー等）は
                                    // ComponentMeta の定義から作られる。
                                    ComponentGuiRegistry::Instance().Draw(td.id, data, id, world);

                                    ImGui::Spacing();

                                    // 生のメモリは折りたたんで出す
                                    if (ImGui::TreeNodeEx("Raw Memory (Hex Dump)"))
                                    {
                                        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
                                        DrawHexDump(data, td.typeSize);
                                        ImGui::PopFont();
                                        ImGui::TreePop();
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
    }
    else
    {
        ImGui::TextDisabled("Select a chunk on the left to see details.");
    }
    ImGui::Columns(1);
}

// ============================================================
// Execution Graph
//   TaskScheduler が決めたバッチを上から順に描く。
//   同じバッチ内の System は依存が無く、ワーカースレッドで並列に実行される。
//   バッチ同士は上から順に直列に実行される（前のバッチの完了を待つ）。
// ============================================================
void ECSDebugWindow::DrawExecutionGraphTab(CCL::ECS::SystemManager *systemManager)
{
    ImGui::TextWrapped(
        "TaskScheduler が解決した並列実行バッチ。同じバッチ内は並列（ワーカースレッド）、"
        "バッチ同士は上から順に直列に実行される。");
    ImGui::Spacing();

    auto drawGraph = [](const char *title, const CCL::ECS::TaskScheduler &scheduler) {
        const auto &batches = scheduler.GetBatches();

        // 総 System 数と、最大の並列幅（同時に走る最大数）
        size_t totalSystems = 0;
        size_t maxWidth     = 0;
        for (const auto &b : batches)
        {
            totalSystems += b.size();
            if (b.size() > maxWidth) maxWidth = b.size();
        }

        ImGui::SeparatorText(title);
        if (batches.empty())
        {
            ImGui::TextDisabled("Graph not built yet or no systems registered.");
            return;
        }

        ImGui::Text("Batches: %zu    Systems: %zu    Max Parallel Width: %zu",
                    batches.size(), totalSystems, maxWidth);
        ImGui::Spacing();

        for (size_t i = 0; i < batches.size(); ++i)
        {
            const auto &batch    = batches[i];
            const bool  parallel = batch.size() > 1;

            // 並列のバッチ（複数スレッドで同時に実行）は緑、1つだけのバッチは灰。
            const ImVec4 col = parallel ? ImVec4(0.40f, 0.90f, 0.50f, 1.0f)
                                        : ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
            ImGui::TextColored(col, "Batch %zu  [%s x%zu]",
                               i, parallel ? "Parallel" : "Serial", batch.size());

            ImGui::Indent();
            for (auto *sys : batch)
            {
                ImGui::BulletText("%s", sys->GetName().c_str());
            }
            ImGui::Unindent();
            ImGui::Spacing();
        }
    };

    drawGraph("Logic Systems Graph", systemManager->GetLogicScheduler());
    ImGui::Spacing();
    drawGraph("Render Systems Graph", systemManager->GetRenderScheduler());
}
