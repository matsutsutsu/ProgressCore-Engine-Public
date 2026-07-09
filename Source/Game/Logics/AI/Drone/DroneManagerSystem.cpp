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

void DroneManagerSystem::ReindexAllDrones()
{
    // まず総数を数える
    int total = GetAliveCount();
    if (total <= 0) return;

    // 連番を振る（0,1,2,... と totalDrones=total）
    uint16_t idx  = 0;
    auto     view = _world->View<DroneComponent>();
    for (auto e : view)
    {
        if (auto *d = _world->GetComponent<DroneComponent>(e))
        {
            d->localIndex  = idx++;
            d->totalDrones = static_cast<uint16_t>(total);
        }
    }
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
    if (bossId == InvalidEntityID)
    {
        CCL_LOG_WARN(LogCategory::Game, "[DroneManager] ボスが居ないので生成中止");
        return 0;
    }

    auto   *bossTrans = _world->GetComponent<TransformComponent>(bossId);
    Vector3 basePos   = bossTrans ? Vector3(bossTrans->position) : Vector3::Zero;
    basePos.y += 5.0f;

    int spawned = 0;
    for (int i = 0; i < count; ++i)
    {
        EntityID droneId = Prefab::SpawnPrefab(*_world, kDronePrefab);
        if (droneId == InvalidEntityID) continue;

        if (auto *t = _world->GetComponent<TransformComponent>(droneId))
        {
            t->position = basePos;
            t->isDirty  = true;
        }
        if (auto *d = _world->GetComponent<DroneComponent>(droneId))
        {
            d->ownerBossId = bossId;
            // ★動作確認用：円陣を組ませる
            d->currentFormation = DroneFormationType::OrbitCircle;
            d->orbitRadius      = 8.0f;
        }
        if (auto *tm = _world->GetComponent<TimeStateComponent>(droneId))
        {
            tm->group = TimeGroup::Enemy;
        }
        ++spawned;
    }

    // ★スポーン後：全ドローンに localIndex / totalDrones を振り直す
    //   （既存＋新規をまとめて連番にしないと円が歪む）
    ReindexAllDrones();

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

void DroneManagerSystem::SetAllDroneState(DroneState state)
{
    int  count = 0;
    auto view  = _world->View<DroneComponent>();
    for (auto e : view)
    {
        if (auto *d = _world->GetComponent<DroneComponent>(e))
        {
            d->currentState = state;

            // FireCharge に切り替える場合、突撃に必要な最低限のデータを補う
            if (state == DroneState::FireCharge)
            {
                // fireDirection が未設定(0ベクトル)だと動かないので、
                // ボスからプレイヤー方向 or 単純にボス→ドローンの外向きベクトルを与える
                if (d->fireDirection.LengthSquared() < 0.001f)
                {
                    d->fireDirection = DirectX::SimpleMath::Vector3::UnitZ; // 最低限のフェイルセーフ
                }
                d->stateTimer = 0.5f; // タメ0.5秒 → リリース、という動作確認向けの値
            }
            ++count;
        }
    }
    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] 全 %d 機の State を %d に変更", count, (int)state);
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

         ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "State Debug (動作確認用)");

        if (ImGui::Button("All -> Idle", ImVec2(110, 26)))
            SetAllDroneState(DroneState::Idle);

        ImGui::SameLine();
        if (ImGui::Button("All -> MoveToTarget", ImVec2(140, 26)))
            SetAllDroneState(DroneState::MoveToTarget);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        if (ImGui::Button("All -> FireCharge (突撃!)", ImVec2(180, 26)))
            SetAllDroneState(DroneState::FireCharge);
        ImGui::PopStyleColor();

        ImGui::Unindent();
    }
#endif
}


// L02_Update で計数（StateTreeSystem と同じステージ。順序は問わない）
REGISTER_LOGIC_SYSTEM(DroneManagerSystem, Priority::LogicStage::L02_Update)