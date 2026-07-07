/// @file  DroneManagerSystem.cpp
/// @brief ドローンの生成・計数・破壊の実装。既存 DroneFormationSystem::OnGui の
///        生成ロジックを移植し、BattleContext への計数反映を追加した。
#include "DroneManagerSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include <SimpleMath.h>


#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// ドローンのプレハブパス（既存コードから）
static constexpr const char* kDronePrefab = "Assets/Prefabs/Enemy/Drone.json";


// ============================================================================
// Update：生存数を数えて BattleContext に反映（毎フレーム）
// ============================================================================
void DroneManagerSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<BattleContext>()) return;
    auto& battle = _world->GetResource<BattleContext>();

    // 生存ドローンを数える
    int alive = GetAliveCount();

    // 「ドローン数 = ボスHP」を BattleContext に反映
    // → これで司令官StateTree の DroneCountBelowCondition が本物の数を見る
    battle.droneAliveCount = alive;
}


// ============================================================================
// GetAliveCount：View で数えるだけ
// ============================================================================
int DroneManagerSystem::GetAliveCount() const
{
    int count = 0;
    auto view = _world->View<DroneComponent>();
    for (auto e : view) { (void)e; ++count; }
    return count;
}


// ============================================================================
// FindBoss：最初のボスを探す
// ============================================================================
EntityID DroneManagerSystem::FindBoss() const
{
    auto bossView = _world->View<BossCommandComponent, TransformComponent>();
    for (auto b : bossView) return b;   // 最初の1体
    return InvalidEntityID;
}


// ============================================================================
// SpawnDrones：既存の生成ロジックを移植
// ============================================================================
int DroneManagerSystem::SpawnDrones(int count)
{
    EntityID bossId = FindBoss();
    if (bossId == InvalidEntityID) {
        CCL_LOG_WARN(LogCategory::Game, "[DroneManager] ボスが居ないので生成中止");
        return 0;
    }

    auto* bossTrans = _world->GetComponent<TransformComponent>(bossId);
    Vector3 basePos = bossTrans ? Vector3(bossTrans->position) : Vector3::Zero;
    basePos.y += 5.0f;

    int spawned = 0;
    for (int i = 0; i < count; ++i) {
        EntityID droneId = Prefab::SpawnPrefab(*_world, kDronePrefab);
        if (droneId == InvalidEntityID) continue;

        if (auto* t = _world->GetComponent<TransformComponent>(droneId)) {
            t->position = basePos;
            t->isDirty  = true;
        }
        if (auto* d = _world->GetComponent<DroneComponent>(droneId)) {
            d->ownerBossId = bossId;
        }
        if (auto* tm = _world->GetComponent<TimeStateComponent>(droneId)) {
            tm->group = TimeGroup::Enemy;
        }
        ++spawned;
    }

    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] %d 機スポーン（合計 %d）",
        spawned, GetAliveCount());
    return spawned;
}


// ============================================================================
// DestroyDrones：先頭から count 機破壊
// ============================================================================
int DroneManagerSystem::DestroyDrones(int count)
{
    int destroyed = 0;
    auto view = _world->View<DroneComponent>();
    for (auto e : view) {
        if (destroyed >= count) break;
        _world->Destroy(e);
        ++destroyed;
    }
    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] %d 機破壊", destroyed);
    return destroyed;
}


// ============================================================================
// DestroyAll
// ============================================================================
void DroneManagerSystem::DestroyAll()
{
    auto view = _world->View<DroneComponent>();
    for (auto e : view) _world->Destroy(e);
    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] 全機破壊");
}


// ============================================================================
// OnGui：生成・破壊・カウント確認のデバッグUI
// ============================================================================
void DroneManagerSystem::OnGui()
{
#ifdef ENABLE_EDITOR
    if (ImGui::CollapsingHeader("Drone Manager (Spawn / Count / Destroy)",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Spawn Settings");
        ImGui::SliderInt("Spawn Count", &_spawnCount, 1, 100);

        // 現在数を表示（BattleContext と View 両方）
        int alive = GetAliveCount();
        ImGui::Text("Current Active Drones: %d", alive);

        if (_world->HasResource<BattleContext>()) {
            auto& b = _world->GetResource<BattleContext>();
            ImGui::Text("BattleContext.droneAliveCount: %d", b.droneAliveCount);
            const char* tactic =
                b.droneAliveCount <= 100 ? "Final" :
                b.droneAliveCount <= 400 ? "Aggressive" :
                b.droneAliveCount <= 700 ? "Pressure" : "Control";
            ImGui::Text("→ 期待戦術: %s", tactic);
        }

        ImGui::Spacing();

        if (ImGui::Button("Spawn Drones", ImVec2(120, 30)))
            SpawnDrones(_spawnCount);

        ImGui::SameLine();

        if (ImGui::Button("Destroy 50", ImVec2(100, 30)))
            DestroyDrones(50);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Destroy All", ImVec2(100, 30)))
            DestroyAll();
        ImGui::PopStyleColor();

        ImGui::Unindent();
    }
#endif
}


// L02_Update で計数（StateTreeSystem と同じステージ。順序は問わない）
REGISTER_LOGIC_SYSTEM(DroneManagerSystem, Priority::LogicStage::L02_Update)