#include "DroneManagerSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Squad/SquadUpdateSystem.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Serialization/Factory/Prefab.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Components/EntityLabel.h"
#include "Engine/Cinematic/CinematicEvents.h"
#include <SimpleMath.h>
#include <algorithm>

#include <imgui.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr const char *kDronePrefab = "Assets/Prefabs/Enemy/Drone.json";

    constexpr int kInitialSpawnCount   = 120; // シーン開始時の配備数。高度合成数を参考に120にした
    constexpr int kReinforceSpawnCount = 120; // 第2形態の増援。初期と同数で、群れを2倍にする

    // 増援は 0.1 秒ごとに 8 機ずつ、1.5 秒かけて出す
    // 一度に出すと 80m/s で一瞬で持ち場に着き、リングが急に濃くなるだけに見える
    constexpr int   kReinforceBatch    = 8;
    constexpr float kReinforceInterval = 0.1f;
} // namespace

// ---------------------------------------------------------------------------
// 毎ステップと自動配備
// ---------------------------------------------------------------------------

void DroneManagerSystem::Initialize()
{
    // シグナルはムービーの段から届くので、ここでは印だけ付け、生成は Update で行う
    ListenEvent<CinematicSignalEvent>([this](const CinematicSignalEvent &ev) {
        if (ev.name == kReinforceSignal) m_reinforceRequested = true;
    });
}



void DroneManagerSystem::Update(float dt)
{
    TryAutoSpawnInitial();
    TryReinforcePhase2(dt);

    if (!m_world->HasResource<BattleContext>()) return;
    auto &battle = m_world->GetResource<BattleContext>();

    int alive = GetAliveCount();

    battle.droneAliveCount = alive;
}



void DroneManagerSystem::TryAutoSpawnInitial()
{
    if (m_autoSpawnDone) return;

    if (FindBoss() == InvalidEntityID) return;

    int spawned = SpawnDrones(kInitialSpawnCount);
    if (spawned > 0)
    {
        m_autoSpawnDone = true;
        CCL_LOG_INFO(LogCategory::Game, "[DroneManager] シーン開始：初期ドローン %d 機を自動配備", spawned);
    }
}



void DroneManagerSystem::TryReinforcePhase2(float dt)
{
    // 処理フロー:
    // 1. 増援を始めるかを決める
    // 2. 間隔ごとに一団ずつ出す

    // ---------------------------------------------------------
    // 1. 始める
    // ---------------------------------------------------------

    // ムービーをスキップするとシグナルは出ない。Boss Debugger で第2形態へ飛ばすとムービー自体が流れない
    // どちらの場合も、第2形態に入った時点で始める
    if (!m_reinforceStarted)
    {
        if (!m_reinforceRequested && !IsBossInPhase2()) return;

        m_reinforceStarted = true;
        m_reinforceLeft    = kReinforceSpawnCount;
        m_reinforceTimer   = 0.0f;
        CCL_LOG_INFO(LogCategory::Game, "[DroneManager] 第2形態：増援 %d 機の配備を開始", kReinforceSpawnCount);
    }

    // ---------------------------------------------------------
    // 2. 一団ずつ出す
    // ---------------------------------------------------------
    if (m_reinforceLeft <= 0) return;

    m_reinforceTimer -= dt;
    if (m_reinforceTimer > 0.0f) return;
    m_reinforceTimer += kReinforceInterval;

    // ボスがいなくて出せなかったときは、次の間隔でやり直す
    m_reinforceLeft -= SpawnDrones((std::min)(kReinforceBatch, m_reinforceLeft));
}



// ---------------------------------------------------------------------------
// 数え上げと番号の振り直し
// ---------------------------------------------------------------------------

int DroneManagerSystem::GetAliveCount() const
{
    // View だと機数ぶんの vector を作るので、数えるだけの ViewEach にする
    int count = 0;
    m_world->ViewEach<DroneComponent>([&count](EntityID) { ++count; });
    return count;
}



void DroneManagerSystem::ReindexAllDrones()
{
    int total = GetAliveCount();
    if (total <= 0) return;

    uint16_t idx  = 0;
    auto     view = m_world->View<DroneComponent>();
    for (auto e : view)
    {
        auto *d = m_world->GetComponent<DroneComponent>(e);
        if (!d) continue;

        d->localIndex  = idx++;
        d->totalDrones = static_cast<uint16_t>(total);
    }

    SquadUpdateSystem::AssignNewMembers(*m_world);
}



EntityID DroneManagerSystem::FindBoss() const
{
    auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
    for (auto b : bossView)
        return b;
    return InvalidEntityID;
}



bool DroneManagerSystem::IsBossInPhase2() const
{
    const EntityID boss = FindBoss();
    if (boss == InvalidEntityID) return false;

    const auto *phase = m_world->GetComponent<BossPhaseComponent>(boss);
    return phase && phase->phase == BossPhase::Combat2;
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
            d->currentFormation = DroneFormationType::HighOrbit;
            d->orbitRadius      = 8.0f;
            d->hasReachedSlot   = false;

            // ホバーの位相を機体ごとにずらす。揃っていると群れが1枚の板として揺れる
            // EntityID は続き番号なので、ハッシュでかき混ぜないと隣同士が揃って揺れる
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

    // 既存＋新規をまとめて連番にしないと円が歪む
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

    // 押し間違えないよう赤くする
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("全機破壊", ImVec2(100, 30)))
        DestroyAll();
    ImGui::PopStyleColor();
}



REGISTER_LOGIC_SYSTEM(DroneManagerSystem, Priority::LogicStage::L02_Update)
