/// @file  DroneManagerSystem.cpp
/// @brief ドローンの生成・計数・破壊の実装。既存 DroneFormationSystem::OnGui の
///        生成ロジックを移植し、BattleContext への計数反映を追加した。
#include "DroneManagerSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent
#include "Game/Logics/AI/StateTree/Core/StateTreeComponent.h"
#include "Game/Logics/Character/Player/Components/JustEvadeComponent.h" // チェイン段数の参照
#include "Engine/Transform/Components/TransformComponent.h"
#include <vector>
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

// ノード種別はプロセス内で1回だけ登録（NodeRegistry はシングルトン）
static const DroneAI::DroneAIIDs kDroneAIIds = DroneAI::RegisterNodes();


void DroneManagerSystem::EnsureDroneAIRegistered()
{
    // EditorScene::Initialize が StateTreeRegistry を後から作り直すため、
    // 毎フレーム「DroneAI が未登録なら登録」する遅延方式にする。
    if (!m_world->HasResource<CCL::AI::StateTreeRegistry>()) return;

    auto& stReg = m_world->GetResource<CCL::AI::StateTreeRegistry>();
    if (stReg.FindByName("DroneAI").IsValid()) return;

    stReg.Register(DroneAI::BuildDroneAsset(kDroneAIIds));
    CCL_LOG_INFO(LogCategory::AI, "[DroneManager] DroneAI(StateTree) 登録完了");
}


void DroneManagerSystem::Initialize()
{
    // ノード種別は static 初期化で登録済み。アセットは EnsureDroneAIRegistered で遅延登録。
    EnsureDroneAIRegistered();

    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        OnJustEvadeSuccess(ev);
    });
}

// ============================================================================
// Update：生存数を数えて BattleContext に反映（毎フレーム）
// ============================================================================
void DroneManagerSystem::Update(float /*dt*/)
{
    EnsureDroneAIRegistered();

    // シーン開始時：ボスとRegistryが揃い次第、初期ドローンを1回だけ配備する。
    TryAutoSpawnInitial();

    if (!m_world->HasResource<BattleContext>()) return;
    auto& battle = m_world->GetResource<BattleContext>();

    // 生存ドローンを数える
    int alive = GetAliveCount();

    // 「ドローン数 = ボスHP」を BattleContext に反映
    // → これで司令官StateTree の DroneCountBelowCondition が本物の数を見る
    battle.droneAliveCount = alive;
}


// ============================================================================
// TryAutoSpawnInitial：シーン開始時の初期配備（1回だけ）
// ============================================================================
void DroneManagerSystem::TryAutoSpawnInitial()
{
    if (!m_autoSpawnEnabled || m_autoSpawnDone) return;

    // ボスが未生成 or Registry未準備のフレームは何もせず次フレームに再試行。
    if (!m_world->HasResource<CCL::AI::StateTreeRegistry>()) return;
    if (FindBoss() == InvalidEntityID) return;

    int spawned = SpawnDrones(m_initialSpawnCount);
    if (spawned > 0)
    {
        m_autoSpawnDone = true;   // 成功したらラッチして二度と自動生成しない
        CCL_LOG_INFO(LogCategory::Game,
            "[DroneManager] シーン開始：初期ドローン %d 機を自動配備", spawned);
    }
}


// ============================================================================
// GetAliveCount：View で数えるだけ
// ============================================================================
int DroneManagerSystem::GetAliveCount() const
{
    int count = 0;
    auto view = m_world->View<DroneComponent>();
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
    auto     view = m_world->View<DroneComponent>();
    for (auto e : view)
    {
        if (auto *d = m_world->GetComponent<DroneComponent>(e))
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
    auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
    for (auto b : bossView) return b;   // 最初の1体
    return InvalidEntityID;
}


// ============================================================================
// SpawnDrones：既存の生成ロジックを移植
// ============================================================================
int DroneManagerSystem::SpawnDrones(int count)
{
    EnsureDroneAIRegistered();

    EntityID bossId = FindBoss();
    if (bossId == InvalidEntityID)
    {
        CCL_LOG_WARN(LogCategory::Game, "[DroneManager] ボスが居ないので生成中止");
        return 0;
    }

    auto   *bossTrans = m_world->GetComponent<TransformComponent>(bossId);
    Vector3 basePos   = bossTrans ? Vector3(bossTrans->position) : Vector3::Zero;
    basePos.y += 5.0f;

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
            // 生成時の初期陣形：ボスの頭上高くで旋回する HighOrbit。
            d->currentFormation = DroneFormationType::HighOrbit;
            d->orbitRadius      = 8.0f;
            d->hasReachedSlot   = false;   // スポーン直後は必ず MoveToSlot から開始
        }
        if (auto *tm = m_world->GetComponent<TimeStateComponent>(droneId))
        {
            tm->group = TimeGroup::Enemy;
        }

        // 個体 StateTree を使わせる（プレハブに無ければ追加）
        if (auto *st = m_world->GetComponent<StateTreeComponent>(droneId))
        {
            st->assetName   = "DroneAI";
            st->assetHandle = {};   // 再解決させる
        }
        else
        {
            StateTreeComponent stComp;
            stComp.assetName = "DroneAI";
            m_world->AddComponent<StateTreeComponent>(droneId, stComp);
        }

        ++spawned;
    }

    // AddComponent の遅延反映 + assetHandle 解決
    m_world->ScrutinyAndApply();

    if (m_world->HasResource<CCL::AI::StateTreeRegistry>())
    {
        auto& stReg = m_world->GetResource<CCL::AI::StateTreeRegistry>();
        auto  view  = m_world->View<DroneComponent, StateTreeComponent>();
        for (auto e : view)
        {
            if (auto *st = m_world->GetComponent<StateTreeComponent>(e))
            {
                if (st->assetName == "DroneAI" && !st->assetHandle.IsValid())
                {
                    st->assetHandle = stReg.FindByName("DroneAI");
                }
            }
        }
    }

    // スポーン後：全ドローンに localIndex / totalDrones を振り直す
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
    auto view = m_world->View<DroneComponent>();
    for (auto e : view) {
        if (destroyed >= count) break;
        m_world->Destroy(e);
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
    auto view = m_world->View<DroneComponent>();
    for (auto e : view) m_world->Destroy(e);
    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] 全機破壊");
}

void DroneManagerSystem::SetAllDroneState(DroneState state)
{
    int  count = 0;
    auto view  = m_world->View<DroneComponent>();
    for (auto e : view)
    {
        if (auto *d = m_world->GetComponent<DroneComponent>(e))
        {
            d->currentState = state;

            if (state == DroneState::MoveToTarget)
            {
                d->hasReachedSlot = false;   // 再接近を許可
            }
            else if (state == DroneState::Idle)
            {
                d->hasReachedSlot = true;    // 手動 Idle = 到着済み扱い
            }

            // FireCharge に切り替える場合、突撃に必要な最低限のデータを補う
            if (state == DroneState::FireCharge)
            {
                // BattleContext からプレイヤー位置を取得して方向を計算
                DirectX::SimpleMath::Vector3 dir = -DirectX::SimpleMath::Vector3::UnitZ; // フェイルセーフ
                if (m_world->HasResource<BattleContext>())
                {
                    auto                        &battle   = m_world->GetResource<BattleContext>();
                    DirectX::SimpleMath::Vector3 toPlayer = battle.playerPos - d->targetPosition;
                    toPlayer.y                            = 0.0f; // 水平方向のみ
                    if (toPlayer.LengthSquared() > 0.001f)
                    {
                        toPlayer.Normalize();
                        dir = toPlayer;
                    }
                }
                d->fireDirection       = dir;
                d->targetLookDirection = dir; // 見た目の向きも合わせる
                d->stateTimer          = 0.5f;
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

        // ---- シーン開始時の自動初期配備 ----
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "Auto Initial Spawn (シーン開始時)");
        ImGui::Checkbox("Enable Auto Spawn", &m_autoSpawnEnabled);
        ImGui::SliderInt("Initial Count", &m_initialSpawnCount, 1, 1000);
        ImGui::Text("Auto Spawn Done: %s", m_autoSpawnDone ? "yes" : "no");
        if (ImGui::Button("Re-arm Auto Spawn", ImVec2(160, 24)))
            m_autoSpawnDone = false;   // もう一度シーン開始時のように配備し直す
        ImGui::Separator();

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Spawn Settings");
        ImGui::SliderInt("Spawn Count", &m_spawnCount, 1, 100);

        // 現在数を表示（BattleContext と View 両方）
        int alive = GetAliveCount();
        ImGui::Text("Current Active Drones: %d", alive);

        if (m_world->HasResource<BattleContext>()) {
            auto& b = m_world->GetResource<BattleContext>();
            ImGui::Text("BattleContext.droneAliveCount: %d", b.droneAliveCount);
            const char* tactic =
                b.droneAliveCount <= 100 ? "Final" :
                b.droneAliveCount <= 400 ? "Aggressive" :
                b.droneAliveCount <= 700 ? "Pressure" : "Control";
            ImGui::Text("→ 期待戦術: %s", tactic);
        }

        ImGui::Spacing();

        if (ImGui::Button("Spawn Drones", ImVec2(120, 30)))
            SpawnDrones(m_spawnCount);

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

        // ================================================================
        // 陣形攻撃テスト（縦割り）：BTを組まずに手動でパイプラインを検証する
        //   ボスの BossCommandComponent へ直接命令を書き、
        //   DroneCommandSystem → DroneAI(StateTree) → Formation の流れを確認する。
        //   ※ボスBTが毎フレーム陣形を指示している場合はそちらに上書きされるので、
        //     テスト時はボスBTを Move のみ等にしておくこと。
        // ================================================================
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Formation Attack Test (縦割り)");

        auto setBossCmd = [this](DroneFormationType f, bool attack) {
            EntityID boss = FindBoss();
            if (auto *cmd = m_world->GetComponent<BossCommandComponent>(boss))
            {
                cmd->requestFormation  = f;
                cmd->droneAttackActive = attack;
            }
        };

        if (ImGui::Button("1) Form Blade (刃を形成)", ImVec2(180, 26)))
            setBossCmd(DroneFormationType::GuillotineSlam, false);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        if (ImGui::Button("2) Strike! (振り下ろし)", ImVec2(180, 26)))
            setBossCmd(DroneFormationType::GuillotineSlam, true);
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button("3) Return (周回へ)", ImVec2(140, 26)))
            setBossCmd(DroneFormationType::OrbitCircle, false);

        ImGui::Unindent();
    }
#endif
}

// イベントハンドラ：ジャスト回避で攻撃元ドローンを破壊し、チェイン段数に応じて
//                   その周囲の「攻撃中(FireCharge)」ドローンを巻き込み撃破する。
void DroneManagerSystem::OnJustEvadeSuccess(const JustEvadeSuccessEvent &ev)
{
    // 撃破の中心＝回避した攻撃元の位置（ボス本体など座標が取れない/ドローンでない場合は単体処理のみ）。
    Vector3 center;
    bool    haveCenter = false;
    if (auto *t = m_world->GetComponent<TransformComponent>(ev.attackerID))
    {
        center     = t->position;
        haveCenter = true;
    }

    // まず攻撃元を破壊（DroneComponent を持たなければ内部で安全に弾かれる）。
    DestroyDrone(ev.attackerID);

    if (!haveCenter) return;

    // プレイヤーのチェイン段数から撃破半径を決める（段数が上がるほど範囲が広がる＝リターン）。
    int   chain = 1;
    float rBase = 3.0f;
    float rPer  = 2.0f;
    if (auto *evade = m_world->GetComponent<JustEvadeComponent>(ev.playerID))
    {
        chain = (evade->chainCount > 0) ? evade->chainCount : 1;
        rBase = evade->killRadiusBase;
        rPer  = evade->killRadiusPerLevel;
    }
    const float radius   = rBase + (chain - 1) * rPer;
    const float radiusSq = radius * radius;

    // イテレート中の破棄を避けるため、対象を一旦集めてから破壊する。
    std::vector<EntityID> toKill;
    auto view = m_world->View<DroneComponent, TransformComponent>();
    for (EntityID id : view)
    {
        if (id == ev.attackerID) continue;
        auto *d = m_world->GetComponent<DroneComponent>(id);
        if (!d || d->currentState != DroneState::FireCharge) continue; // 「攻撃中だけが弱点」を維持
        auto *t = m_world->GetComponent<TransformComponent>(id);
        if (!t) continue;
        if (Vector3::DistanceSquared(center, t->position) <= radiusSq)
            toKill.push_back(id);
    }
    for (EntityID id : toKill) DestroyDrone(id);

    if (!toKill.empty())
        CCL_LOG_INFO(LogCategory::Game, "[DroneManager] チェイン範囲撃破 chain=%d r=%.1f -> 追加%zu機",
                     chain, radius, toKill.size());
}

// 指定した1機を破壊する
bool DroneManagerSystem::DestroyDrone(EntityID droneId)
{
    // ドローンでないもの（ボス等）を誤って壊さないためのガード
    if (!m_world->GetComponent<DroneComponent>(droneId))
    {
        return false;
    }

    m_world->Destroy(droneId);

    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] ドローン破壊 id=%llu（残り %d 機）",
        (unsigned long long)droneId, GetAliveCount() - 1);
    //   ↑ GetAliveCount() はまだ破棄が反映される前なので -1 して見た目のログを合わせている
    //     （Destroy が即座に消えるか遅延キューかはエンジン仕様に依存。
    //       次フレームの Update で正確な数に上書きされるので実害はない）

    return true;
}

// L02_Update で計数（StateTreeSystem と同じステージ。順序は問わない）
REGISTER_LOGIC_SYSTEM(DroneManagerSystem, Priority::LogicStage::L02_Update)