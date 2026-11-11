#include "Game/Logics/Combat/Systems/HealthSystem.h"
#include "Game/Logics/Combat/Components/DeadTag.h"
#include "Engine/Core/Components/NameComponent.h"
#include <algorithm>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <imgui.h>

void HealthSystem::Update(float dt)
{
    ForEachWithID([&](CCL::ECS::EntityID id, HealthComponent &health,
                      const TimeStateComponent &time) {
        const float ldt = time.localDt;

        if (health.invincibilityTimer > 0.0f)
            health.invincibilityTimer -= ldt;

        if (health.currentHealth <= 0.0f)
        {
            health.currentHealth = 0.0f;
            // 付いているのにもう一度 AddComponent しないよう確かめる
            if (!m_world->HasComponent<Tag::DeadTag>(id))
                m_world->AddComponent<Tag::DeadTag>(id);
        }
    });
}



void HealthSystem::OnGui()
{
    ImGui::Text("Health Monitor");
    ImGui::Separator();

    // 生きている数と倒れた数
    int aliveCount = 0;
    int deadCount  = 0;
    ForEachWithID([&](CCL::ECS::EntityID id, HealthComponent &health,
                      const TimeStateComponent & /*time*/) {
        if (m_world->HasComponent<Tag::DeadTag>(id))
            ++deadCount;
        else
            ++aliveCount;
    });

    ImGui::Text("Alive: %d  /  Dead(pending): %d", aliveCount, deadCount);
    ImGui::Separator();

    // エンティティごとの表
    if (ImGui::BeginTable("HealthTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 300.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("EntityID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Invincible", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        ForEachWithID([&](CCL::ECS::EntityID id, HealthComponent &health,
                          const TimeStateComponent & /*time*/) {
            const bool isDead = m_world->HasComponent<Tag::DeadTag>(id);

            ImGui::TableNextRow();

            // 倒れたエンティティの行は赤くする
            if (isDead)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       IM_COL32(160, 30, 30, 120));

            // 名前（NameComponent がなければ ID を出す）
            ImGui::TableSetColumnIndex(0);
            {
                const auto *nameComp = m_world->GetComponent<NameComponent>(id);
                if (nameComp && nameComp->name[0] != '\0')
                    ImGui::TextUnformatted(nameComp->name);
                else
                    ImGui::Text("ID:%llu", static_cast<unsigned long long>(id));
            }

            // HP バー
            ImGui::TableSetColumnIndex(1);
            {
                const float ratio = (health.maxHealth > 0.0f)
                                        ? (health.currentHealth / health.maxHealth)
                                        : 0.0f;

                // HP の割合で色を変える（50% より上は緑、25% より上は黄、それ以下は赤）
                const ImVec4 color = (ratio > 0.5f)
                                         ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
                                     : (ratio > 0.25f)
                                         ? ImVec4(0.9f, 0.7f, 0.1f, 1.0f)
                                         : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);

                char overlay[32];
                snprintf(overlay, sizeof(overlay), "%.0f / %.0f",
                         health.currentHealth, health.maxHealth);
                ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), overlay);

                ImGui::PopStyleColor();
            }

            // 無敵時間
            ImGui::TableSetColumnIndex(2);
            if (health.invincibilityTimer > 0.0f)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "%.2fs", health.invincibilityTimer);
            else
                ImGui::TextDisabled("-");

            // 状態
            ImGui::TableSetColumnIndex(3);
            if (isDead)
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "DEAD");
            else
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Alive");
        });

        ImGui::EndTable();
    }
}



REGISTER_LOGIC_SYSTEM(HealthSystem, Priority::LogicStage::L06_HealthManagement);
