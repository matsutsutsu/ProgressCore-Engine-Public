#include "ProtoDebugUISystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <imgui.h>

#include "Game/Prototype/PrototypeConfig.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

using namespace CCL::ECS;

void ProtoDebugUISystem::Initialize()
{
    hasGui = true;

    // Resourceが存在しなければ初期化して登録
    if (!_world->HasResource<PrototypeConfig>())
    {
        _world->AddResource<PrototypeConfig>(PrototypeConfig{});
    }
}

void ProtoDebugUISystem::Update(float /*dt*/)
{
    // 描画ロジックは OnGui へ移動したため Update は空
}

// ★修正: ImGui::Begin / End を排除し、中身のUI要素のみを配置
void ProtoDebugUISystem::OnGui()
{
    if (!_world->HasResource<PrototypeConfig>()) return;
    auto &config = _world->GetResource<PrototypeConfig>();

    // --- 1. ダッシュボード（統計情報） ---
    ImGui::SeparatorText("Statistics");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Drones: %d / %d", config.currentDroneCount, config.targetDroneCount);
    ImGui::Text("Combo: %d (Max: %d)", config.currentCombo, config.maxCombo);
    ImGui::Text("Total Kills: %d", config.totalKills);
    ImGui::Text("Damage Taken (Hits): %d", config.hitCount);

    // --- 2. プレイヤー操作のチューニング ---
    // (必要に応じてコメントアウトを外して使用)
    /*
    ImGui::SeparatorText("Player Tuning (Realtime)");
    _world->ForEachWithID<PlayerComponent, DashComponent>([&](EntityID, PlayerComponent &, DashComponent &dash) {
        ImGui::DragFloat("Dash Speed", &dash.dashSpeed, 0.5f, 10.0f, 100.0f);
        ImGui::DragFloat("Dash Duration", &dash.dashDuration, 0.01f, 0.05f, 1.0f);
        ImGui::DragFloat("Just Evade Window", &dash.justEvadeWindow, 0.01f, 0.01f, 0.5f);
        ImGui::DragFloat("Dash Cooldown", &dash.dashCooldown, 0.01f, 0.0f, 2.0f);
    });
    */

    // --- 3. ドローン・敵のチューニング ---
    ImGui::SeparatorText("Drone Swarm Tuning");
    ImGui::DragInt("Target Spawn Count", &config.targetDroneCount, 5, 0, 2000);
    ImGui::Checkbox("Enable Spawning", &config.enableDroneSpawn);
    ImGui::DragFloat("Approach Speed", &config.droneApproachSpeed, 0.5f, 1.0f, 50.0f);
    ImGui::DragFloat("Dash Attack Speed", &config.droneDashSpeed, 0.5f, 5.0f, 100.0f);
    ImGui::DragFloat("Dash Attack Range", &config.droneDashRange, 0.5f, 2.0f, 50.0f);

    // --- 4. ゲームルールのチューニング ---
    ImGui::SeparatorText("Game Rules");
    ImGui::DragInt("Kills Per Evade", &config.killCountPerEvade, 1, 1, 100);
    ImGui::DragFloat("Kill Radius", &config.killRadius, 0.5f, 5.0f, 100.0f);

    if (ImGui::Button("Reset Combo & Stats", ImVec2(-1, 30)))
    {
        config.currentCombo = 0;
        config.maxCombo     = 0;
        config.totalKills   = 0;
        config.hitCount     = 0;
    }
}

// 登録位置は既存のまま
REGISTER_LOGIC_SYSTEM(ProtoDebugUISystem, Priority::LogicStage::L02_PostUpdate);