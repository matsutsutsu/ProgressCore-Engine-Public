// ===========================================================================
// HealthSystem.cpp
// ----------------
// 役割・責務はヘッダを参照。
//
// 実装の意図(Why):
//   - 無敵時間は localDt で減算する。ヒットストップ中は localDt=0 になるため
//     無敵時間の消費も止まり、ヒットストップ演出と自然に整合する。
//   - DeadTag は HasComponent で二重付与ガードをしてから AddComponent する。
//     同フレームに複数のダメージが入った場合でも1度だけ付与される。
//   - OnGui の HP バーは ProgressBar で「今いくつか」を直感的に示す。
//     DeadTag 付与済みの行は赤でハイライトし、消滅直前の状態も見えるようにする。
// ===========================================================================
#include "HealthSystem.h"
#include "Game/Logics/Combat/DeadTag.h"
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

        // 1. 無敵時間のカウントダウン
        if (health.invincibilityTimer > 0.0f)
            health.invincibilityTimer -= ldt;

        // 2. 死亡判定と DeadTag 付与
        if (health.currentHealth <= 0.0f)
        {
            health.currentHealth = 0.0f;
            if (!m_world->HasComponent<Tag::DeadTag>(id))
                m_world->AddComponent<Tag::DeadTag>(id);
        }
    });
}

void HealthSystem::OnGui()
{
    ImGui::Text("Health Monitor");
    ImGui::Separator();

    // 生存数・死亡数の集計
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

    // エンティティごとの詳細一覧
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

            // 死亡エンティティは行を赤くハイライト
            if (isDead)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       IM_COL32(160, 30, 30, 120));

            // 名前（NameComponent があれば name を、なければ ID をフォールバック）
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

                // バーの色を HP に応じて変える（緑→黄→赤）
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
