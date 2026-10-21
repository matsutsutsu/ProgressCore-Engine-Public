#include "CCL_SystemManager.h"
#include "Engine/ECS/Core/CCL_Chunk.h"

#include <imgui.h>

namespace CCL::ECS
{
    void SystemManager::ResolveTargetChunks(ChunkManager *cm)
    {
        ZoneScopedN("SystemManager::ResolveTargetChunks");

        if (!cm) return;

        // 配っている最中に Chunk が増減しないよう、構造変更を止めておく
        std::shared_lock<std::shared_mutex> lock(cm->GetStructureMutex());

        // 1. Chunk の構成が前回から変わったかを判定する。
        //    変わっていなければ全 System × 全 Chunk の総当たりを丸ごと省ける。
        auto &chunks           = cm->GetChunks(); // vector<unique_ptr<Chunk>>&
        bool  isStructureDirty = false;

        if (chunks.size() != m_cachedChunkPointers.size())
        {
            isStructureDirty = true;
        }
        else
        {
            // 数が同じでも、Chunk が解放→再確保されてアドレスが入れ替わっている場合がある
            for (size_t i = 0; i < chunks.size(); ++i)
            {
                if (chunks[i].get() != m_cachedChunkPointers[i])
                {
                    isStructureDirty = true;
                    break;
                }
            }
        }

        if (!isStructureDirty)
        {
            return;
        }

        m_cachedChunkPointers.clear();
        m_cachedChunkPointers.reserve(chunks.size());
        for (auto &chunkPtr : chunks)
        {
            m_cachedChunkPointers.push_back(chunkPtr.get());
        }

        // 2. 全 System の処理対象 Chunk をリセットする
        auto clearTargets = [](auto &systems) {
            for (auto &sysPtr : systems)
            {
                sysPtr->ClearTargetChunks();
            }
        };

        clearTargets(m_logicSystems);
        clearTargets(m_renderSystems);

        // 3. 全 Chunk を走査し、Filter に合う System へ配る
        for (auto &upChunk : chunks)
        {
            if (!upChunk) continue;
            Chunk           *chunk     = upChunk.get();
            const Archetype &chunkArch = chunk->GetArchetype();

            auto matchSystems = [&](auto &systems) {
                for (auto &sysPtr : systems)
                {
                    const Archetype &filter  = sysPtr->GetFilter();
                    const Archetype &exclude = sysPtr->GetExcludeFilter();

                    // Filter が空の System は、除外条件だけで判定する
                    if (filter.empty())
                    {
                        if (ArchetypeHelper::LExcludesR(chunkArch, exclude))
                        {
                            sysPtr->AddTargetChunk(chunk);
                        }
                        continue;
                    }

                    // 必要 Component を全て持ち、かつ除外 Component を1つも持たない
                    if (ArchetypeHelper::LhasR(chunkArch, filter) &&
                        ArchetypeHelper::LExcludesR(chunkArch, exclude))
                    {
                        sysPtr->AddTargetChunk(chunk);
                    }
                }
            };

            matchSystems(m_logicSystems);
            matchSystems(m_renderSystems);
        }
    }

    void SystemManager::OnGui()
    {
        ZoneScopedN("SystemManager::OnGui");

        if (ImGui::Begin("System Manager"))
        {
            // 全 System の並列実行を一括で切り替える（並列起因の不具合の切り分け用）
            if (ImGui::Checkbox("Global Multi-Thread Enable", &globalMultiThreadEnabled))
            {
                for (auto &sys : m_logicSystems)
                    sys->enableMultiThread = globalMultiThreadEnabled;
                for (auto &sys : m_renderSystems)
                    sys->enableMultiThread = globalMultiThreadEnabled;
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Force enable/disable multi-threading for ALL systems.");
            }

            // 順序が不定なペアのログ。グラフはもう作ってあるので、入れた時点の一覧をすぐ出す
            // （次に出るのはシーンを切り替えて作り直したとき）
            if (ImGui::Checkbox("順序不定ペアをログに出す", &logSchedulerAmbiguities) && logSchedulerAmbiguities)
            {
                m_logicScheduler.LogAmbiguities("Logic");
                m_renderScheduler.LogAmbiguities("Render");
            }

            ImGui::Separator();

            if (ImGui::BeginTabBar("SystemManagerTabs"))
            {
                // Monitor タブ: 全 System の処理時間と MT/Debug の切り替えを一覧表示する
                if (ImGui::BeginTabItem("Monitor"))
                {
                    static ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                   ImGuiTableFlags_Resizable |
                                                   ImGuiTableFlags_ScrollY;

                    // 高さ -FLT_MIN でウィンドウ下端まで伸ばし、表の中だけ縦スクロールさせる
                    if (ImGui::BeginTable("SystemMonitorTable", 4, flags, ImVec2(0, -FLT_MIN)))
                    {
                        ImGui::TableSetupColumn(
                            "System Name", ImGuiTableColumnFlags_WidthStretch); // 残りの幅全部
                        ImGui::TableSetupColumn(
                            "Time (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn(
                            "MT", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                        ImGui::TableSetupColumn(
                            "Debug", ImGuiTableColumnFlags_WidthFixed, 40.0f);

                        ImGui::TableHeadersRow();

                        auto drawSystemRow = [](const std::unique_ptr<SystemBase> &sys) {
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(sys->GetName().c_str());

                            // 重い System が一目でわかるよう、2ms 超は赤、0.5ms 超は黄で出す
                            ImGui::TableSetColumnIndex(1);
                            float time = sys->GetDisplayUpdateTime();
                            if (time > 2.0f)
                                ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "%.3f", time);
                            else if (time > 0.5f)
                                ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "%.3f", time);
                            else
                                ImGui::Text("%.3f", time);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::PushID(sys->GetSystemID());
                            // チェックボックスを列の中央に寄せる
                            float cw = ImGui::GetColumnWidth();
                            float iw = ImGui::GetFrameHeight(); // Checkbox の幅
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cw - iw) * 0.5f);
                            ImGui::Checkbox("##mt", &sys->enableMultiThread);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable Multi-Threading");
                            ImGui::PopID();

                            ImGui::TableSetColumnIndex(3);
                            ImGui::PushID(sys->GetSystemID() + 10000); // MT 列の ID と衝突させない
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cw - iw) * 0.5f);
                            ImGui::Checkbox("##vis", &sys->isDebugVisible);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show Debug Draw");
                            ImGui::PopID();
                        };

                        // グループの見出しを1行として挟む
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextDisabled("-- Logic Systems --");
                        for (auto &sys : m_logicSystems)
                            drawSystemRow(sys);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextDisabled("-- Render Systems --");
                        for (auto &sys : m_renderSystems)
                            drawSystemRow(sys);

                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }

                // Logic Inspectors タブ: OnGui を持つ Logic System を折りたたみで並べる
                if (ImGui::BeginTabItem("Logic Inspectors"))
                {
                    for (auto &sys : m_logicSystems)
                    {
                        if (!sys->hasGui) continue;

                        ImGui::PushID(sys->GetSystemID());
                        if (ImGui::CollapsingHeader(sys->GetName().c_str()))
                        {
                            ImGui::Indent();

                            // 各 System の OnGui の負荷を Tracy で個別に見られるようにする
                            {
                                ZoneScoped;
                                std::string zoneName = sys->GetName() + "::OnGui";
                                ZoneName(zoneName.c_str(), zoneName.size());

                                sys->OnGui();
                            }

                            ImGui::Unindent();
                            ImGui::Spacing();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }

                // Render Inspectors タブ: 中身は Logic Inspectors と同じ
                if (ImGui::BeginTabItem("Render Inspectors"))
                {
                    for (auto &sys : m_renderSystems)
                    {
                        if (!sys->hasGui) continue;

                        ImGui::PushID(sys->GetSystemID());
                        if (ImGui::CollapsingHeader(sys->GetName().c_str()))
                        {
                            ImGui::Indent();

                            {
                                ZoneScoped;
                                std::string zoneName = sys->GetName() + "::OnGui";
                                ZoneName(zoneName.c_str(), zoneName.size());

                                sys->OnGui();
                            }

                            ImGui::Unindent();
                            ImGui::Spacing();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

} // namespace CCL::ECS
