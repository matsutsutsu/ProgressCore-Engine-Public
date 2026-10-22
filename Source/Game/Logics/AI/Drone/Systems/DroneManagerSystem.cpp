#include "DroneManagerSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Squad/SquadUpdateSystem.h" // 生成した機を小隊へ入れる
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Components/EntityLabel.h"
#include <SimpleMath.h>

#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr const char *kDronePrefab = "Assets/Prefabs/Enemy/Drone.json";

    constexpr int kInitialSpawnCount = 120; // シーン開始時の配備数。高度合成数を参考に120にした
} // namespace

// ---------------------------------------------------------------------------
// 毎ステップと自動配備
// ---------------------------------------------------------------------------

void DroneManagerSystem::Update(float /*dt*/)
{
    // ボスが揃い次第、最初にドローンを1回だけ配備する。
    TryAutoSpawnInitial();

    if (!m_world->HasResource<BattleContext>()) return;
    auto &battle = m_world->GetResource<BattleContext>();

    int alive = GetAliveCount();

    battle.droneAliveCount = alive;
}

void DroneManagerSystem::TryAutoSpawnInitial()
{
    if (m_autoSpawnDone) return;

    // ボスが未生成のフレームは何もせず次フレームに再試行する。
    if (FindBoss() == InvalidEntityID) return;

    int spawned = SpawnDrones(kInitialSpawnCount);
    if (spawned > 0)
    {
        m_autoSpawnDone = true; // 成功したらラッチして二度と自動生成しない
        CCL_LOG_INFO(LogCategory::Game, "[DroneManager] シーン開始：初期ドローン %d 機を自動配備", spawned);
    }
}

// ---------------------------------------------------------------------------
// 数え上げと番号の振り直し
// ---------------------------------------------------------------------------

int DroneManagerSystem::GetAliveCount() const
{
    // 数えるだけなので確保しない（View だと機数ぶんの vector を作ってから捨てることになる）。

    int count = 0;
    m_world->ViewEach<DroneComponent>([&count](EntityID) { ++count; });
    return count;
}

void DroneManagerSystem::ReindexAllDrones()
{
    int total = GetAliveCount();
    if (total <= 0) return;

    // 全機に連番を振る。
    uint16_t idx  = 0;
    auto     view = m_world->View<DroneComponent>();
    for (auto e : view)
    {
        auto *d = m_world->GetComponent<DroneComponent>(e);
        if (!d) continue;

        d->localIndex  = idx++;
        d->totalDrones = static_cast<uint16_t>(total);
    }

    // 小隊の席は小隊の係が決める。
    SquadUpdateSystem::AssignNewMembers(*m_world);
}

EntityID DroneManagerSystem::FindBoss() const
{
    auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
    for (auto b : bossView)
        return b; // 最初の1体
    return InvalidEntityID;
}

// ---------------------------------------------------------------------------
// 生成と破棄
// ---------------------------------------------------------------------------

int DroneManagerSystem::SpawnDrones(int count)
{
    // 処理フロー:
    // 1. ボスの位置を取る
    // 2. プレハブから生成し、初期値を入れる
    // 3. 既存の機と合わせて番号を振り直す

    // --- ボス ---

    EntityID bossId = FindBoss();
    if (bossId == InvalidEntityID)
    {
        CCL_LOG_WARN(LogCategory::Game, "[DroneManager] ボスが居ないので生成中止");
        return 0;
    }

    auto   *bossTrans = m_world->GetComponent<TransformComponent>(bossId);
    Vector3 basePos   = bossTrans ? Vector3(bossTrans->position) : Vector3::Zero;
    basePos.y += 5.0f;

    // --- 生成 ---

    int spawned = 0;
    for (int i = 0; i < count; ++i)
    {
        EntityID droneId = Prefab::SpawnPrefab(*m_world, kDronePrefab);
        if (droneId == InvalidEntityID) continue;

        if (auto *t = m_world->GetComponent<TransformComponent>(droneId))
        {
            t->position = basePos;
            t->isDirty  = true;
        }
        if (auto *d = m_world->GetComponent<DroneComponent>(droneId))
        {
            d->ownerBossId = bossId;
            // 生成時の初期陣形はボスの頭上高くで旋回する HighOrbit。
            d->currentFormation = DroneFormationType::HighOrbit;
            d->orbitRadius      = 8.0f;
            d->hasReachedSlot   = false; // 生成直後は必ず MoveToTarget から始める

            // ホバーの揺れの位相を個体ごとにずらす。
            // 0 のままだと全機の位相が一致し、群れが1枚の板として揺れる。

            // 位相は EntityID を乗算ハッシュ（2654435761）でかき混ぜた値から決める。
            // 続けて生成した機は EntityID も続き番号になるので、そのまま等間隔に使うと
            // 隣り合う機の位相が近くなり、隣同士が揃って揺れる。
            uint32_t phase = static_cast<uint32_t>(droneId) * 2654435761u;
            phase ^= phase >> 15;
            d->hoverTimeOffset = static_cast<float>(phase % 65536u) * (kHoverPhasePeriod / 65536.0f);
        }
        // すでにPrefabで設定されているのでコメントアウト
        //if (auto *tm = m_world->GetComponent<TimeStateComponent>(droneId))
        //{
        //    tm->group = TimeGroup::Enemy;
        //}

        ++spawned;
    }

    // --- 番号の振り直し ---

    // 既存＋新規をまとめて連番にしないと円が歪む。
    // SpawnPrefab は生成をその場で確定させるので、ここで反映し直さなくても View が新しい機を拾える。
    ReindexAllDrones();

    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] %d 機スポーン（合計 %d）",
                 spawned, GetAliveCount());
    return spawned;
}

int DroneManagerSystem::DestroyDrones(int count)
{
    int  destroyed = 0;
    auto view      = m_world->View<DroneComponent>();
    for (auto e : view)
    {
        if (destroyed >= count) break;
        m_world->Destroy(e);
        ++destroyed;
    }
    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] %d 機破壊", destroyed);
    return destroyed;
}

void DroneManagerSystem::DestroyAll()
{
    auto view = m_world->View<DroneComponent>();
    for (auto e : view)
        m_world->Destroy(e);
    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] 全機破壊");
}

// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

void DroneManagerSystem::OnGui()
{
    ImGui::Text("ドローンの数: %d", GetAliveCount());
    ImGui::SliderInt("生成する数", &m_spawnCount, 1, 100);

    if (ImGui::Button("生成", ImVec2(120, 30)))
        SpawnDrones(m_spawnCount);

    ImGui::SameLine();
    if (ImGui::Button("50機破壊", ImVec2(100, 30)))
        DestroyDrones(50);

    // 全機を消すボタンだけ赤くして、押し間違えないようにする。
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("全機破壊", ImVec2(100, 30)))
        DestroyAll();
    ImGui::PopStyleColor();
}

// 陣形（DroneFormationSystem）と同じ L02_Update。前後は GetRunAfter で決める。
REGISTER_LOGIC_SYSTEM(DroneManagerSystem, Priority::LogicStage::L02_Update)
