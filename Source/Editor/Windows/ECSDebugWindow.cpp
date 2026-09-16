#include "ECSDebugWindow.h"
#include "Engine/ECS/Core/CCL_Chunk.h"

#include "Editor/Inspector/ComponentGuiRegistry.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------
// ECSDebugWindow の実装
// ---------------------------------------------------------
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

        // 実行グラフのタブ
        if (ImGui::BeginTabItem("Execution Graph"))
        {
            DrawExecutionGraphTab(context.systemManager);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// =========================================================
// ヘルパー関数: 型IDから一時的な文字列を生成
// =========================================================
const char *ECSDebugWindow::GetFriendlyTypeName(uint32_t id)
{
    // Registryの既存機能を使って名前を逆引きする
    const std::string &name = ComponentGuiRegistry::Instance().GetComponentName(id);

    // 辞書に登録されていればその名前を返す
    if (!name.empty())
    {
        return name.c_str();
    }

    // エディタ用GUIを持たない隠しコンポーネントなどの場合はIDでフォールバック
    static char buf[64];
    snprintf(buf, sizeof(buf), "Unknown (ID:%u)", id);
    return buf;
}

// =========================================================
// ヘルパー関数: 安全で美しい16進数（Hex）メモリダンプ描画
// =========================================================
static void DrawHexDump(const void *data, size_t size)
{
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    char                 lineBuffer[128];

    // 1行あたり16バイトずつ描画
    for (size_t i = 0; i < size; i += 16)
    {
        int offset = snprintf(lineBuffer, sizeof(lineBuffer), "%04zX: ", i);

        // 16進数部分
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

        // ASCII文字部分（右側に表示）
        offset += snprintf(lineBuffer + offset, sizeof(lineBuffer) - offset, " | ");
        for (size_t j = 0; j < 16 && i + j < size; ++j)
        {
            char c = bytes[i + j];
            // 表示可能な文字ならそのまま、それ以外はピリオド '.' にする
            lineBuffer[offset++] = (c >= 32 && c < 127) ? c : '.';
        }
        lineBuffer[offset] = '\0';

        ImGui::TextDisabled("%s", lineBuffer);
    }
}

// =========================================================
// タブ1: Memory & Chunks (メモリのヒートマップとインスペクタ)
// =========================================================
void ECSDebugWindow::DrawMemoryTab(CCL::ECS::World *world)
{
    auto &chunks = world->GetChunkManager().GetChunks();

    // =======================================================
    // チャンクの全体統計
    // =======================================================
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

    static int selectedChunkIdx = -1;

    ImGui::Columns(2, "MemoryTabSplitter", true);
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.40f);

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Chunk Hierarchy by Archetype");
    ImGui::Separator();

    // --- 左カラム: アーキタイプごとにグループ化されたチャンクリスト ---
    if (ImGui::BeginChild("ChunkList"))
    {

        // 改良 1: チャンクをアーキタイプハッシュごとに分類する
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

            // =========================================================
            //  アーキタイプを構成するコンポーネント名を抽出して連結する
            // =========================================================
            std::string componentNames = "";
            if (!chunkIndices.empty() && chunks[chunkIndices[0]])
            {
                auto &firstChunk = chunks[chunkIndices[0]];
                for (const auto &td : firstChunk->GetTypeDatas())
                {
                    if (!componentNames.empty()) componentNames += ", ";
                    // プレフィックス(Component等)が長すぎる場合は、ここで置換・省略処理を入れるとさらに綺麗になります
                    componentNames += GetFriendlyTypeName(td.id);
                }
            }

            // ツリーノードの表示（例: [TransformComponent, VelocityComponent] (3 Chunks) ）
            // ImGuiの仕様上、名前被りを防ぐために末尾に ##ハッシュ値 をつけて隠しIDにします
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

                    // ヒートマップの描画
                    ImDrawList *drawList = ImGui::GetWindowDrawList();
                    ImVec2      p        = ImGui::GetCursorScreenPos();
                    float       width    = ImGui::GetContentRegionAvail().x;
                    float       height   = 10.0f; // 少し高さを抑えてスタイリッシュに
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
                            ImVec2(p.x + e * boxWidth, p.y), // 隙間を無くしてソリッドなバーにする
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
        { // まずチャンクの存在確認
            if (ImGui::BeginChild("EntityDetailList"))
            {

                // チャンクに含まれるコンポーネントの種類を上部に表示
                ImGui::Text("Components in this chunk:");
                for (const auto &td : chunk->GetTypeDatas())
                {
                    ImGui::BulletText("%s (%zu bytes)", GetFriendlyTypeName(td.id), td.typeSize);
                }
                ImGui::Separator();
                ImGui::Spacing();

                for (size_t e = 0; e < chunk->GetEntityCount(); ++e)
                {
                    // 改良: 削除済みのエンティティはスキップするか、目立たなくする
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

                                    // =========================================================
                                    // 描画は GUI レジストリに委譲する
                                    // =========================================================
                                    // 型ごとのウィジェット（Transform なら Position の x/y/z スライダー等）は
                                    // ComponentMeta の定義から生成される。Undo/Redo のフックも同様。
                                    ComponentGuiRegistry::Instance().Draw(td.id, data, id, world);

                                    ImGui::Spacing();

                                    // =========================================================
                                    // 既存のHexダンプは「Advanced (詳細)」として折りたたんで残す
                                    // =========================================================
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

// ---------------------------------------------------------
//  実行グラフの可視化
//    TaskScheduler が解決した「バッチ」を上から順に描く。
//    同じバッチ内のシステムはワーカースレッドで並列実行される（依存が無い）。
//    バッチは上から順に直列に処理される（前のバッチ完了を待つ）。
// ---------------------------------------------------------
void ECSDebugWindow::DrawExecutionGraphTab(CCL::ECS::SystemManager *systemManager)
{
    ImGui::TextWrapped(
        "TaskScheduler が解決した並列実行バッチ。同じバッチ内は並列（ワーカースレッド）、"
        "バッチ同士は上から順に直列に実行される。");
    ImGui::Spacing();

    auto drawGraph = [](const char *title, const CCL::ECS::TaskScheduler &scheduler) {
        const auto &batches = scheduler.GetBatches();

        // 集計: 総システム数・最大並列幅（＝同時に走る最大本数）
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

            // 並列バッチは緑（複数スレッドで同時実行）、単独バッチは灰（メイン等で直列）。
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
