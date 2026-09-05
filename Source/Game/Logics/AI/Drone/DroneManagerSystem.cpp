// ドローンの生成・計数・破壊の実装。
#include "DroneManagerSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include <vector>
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

// ドローンのプレハブパス
static constexpr const char* kDronePrefab = "Assets/Prefabs/Enemy/Drone.json";

// Update：生存数を数えて BattleContext に反映する（毎フレーム）。
void DroneManagerSystem::Update(float /*dt*/)
{
    // ボスが揃い次第、初期ドローンを1回だけ配備する。
    TryAutoSpawnInitial();

    if (!m_world->HasResource<BattleContext>()) return;
    auto& battle = m_world->GetResource<BattleContext>();

    int alive = GetAliveCount();

    // 「ドローン数 = ボスHP」。ボス側の戦術判断が本物の数を見る。
    battle.droneAliveCount = alive;
}


// シーン開始時の初期配備（1回だけ）。
void DroneManagerSystem::TryAutoSpawnInitial()
{
    if (!m_autoSpawnEnabled || m_autoSpawnDone) return;

    // ボスが未生成のフレームは何もせず次フレームに再試行する。
    if (FindBoss() == InvalidEntityID) return;

    int spawned = SpawnDrones(m_initialSpawnCount);
    if (spawned > 0)
    {
        m_autoSpawnDone = true;   // 成功したらラッチして二度と自動生成しない
        CCL_LOG_INFO(LogCategory::Game,
            "[DroneManager] シーン開始：初期ドローン %d 機を自動配備", spawned);
    }
}


// 数えるだけなので確保しない（View だと機数ぶんの vector を作ってから捨てることになる）。
int DroneManagerSystem::GetAliveCount() const
{
    int count = 0;
    m_world->ViewEach<DroneComponent>([&count](EntityID) { ++count; });
    return count;
}

// 撃破後にこれを呼ばないのは意図的。番号を振り直すと生き残った全機の持ち場が同時に
// 変わり、1機落ちるたびに残り全機が円周上を滑って並び直して撃破の手応えが消える。
// 据え置けば落ちた機の位置がそのまま穴として残る＝「削った」ことが見える
// （totalDrones が実数より多いのはこのための状態）。
// 小隊は SquadUpdateSystem が毎フレーム数え直すので欠員が自動的に詰まる。
void DroneManagerSystem::ReindexAllDrones()
{
    int total = GetAliveCount();
    if (total <= 0) return;

    // 1小隊は8機以下である前提でビットマスクを使う（空きスロット探索用）。
    static_assert(SquadRegistry::kSquadSize <= 8, "小隊の人数が uint8_t のビット数を超えている");

    // 使用中のスロットを小隊ごとに集める。所属は一度決めたら維持する（振り直すと
    // 生き残った機の所属まで入れ替わり、同じ部隊で組んでいるように見えない）。
    uint8_t usedSlots[SquadRegistry::kMaxSquads] = {};
    {
        auto scan = m_world->View<DroneComponent>();
        for (auto e : scan)
        {
            const auto *d = m_world->GetComponent<DroneComponent>(e);
            if (!d) continue;
            if (d->squadId >= SquadRegistry::kMaxSquads) continue;
            if (d->slotInSquad >= SquadRegistry::kSquadSize) continue;
            usedSlots[d->squadId] =
                static_cast<uint8_t>(usedSlots[d->squadId] | (1u << d->slotInSquad));
        }
    }

    // 連番を振る。
    uint16_t idx  = 0;
    auto     view = m_world->View<DroneComponent>();
    for (auto e : view)
    {
        auto *d = m_world->GetComponent<DroneComponent>(e);
        if (!d) continue;

        d->localIndex  = idx++;
        d->totalDrones = static_cast<uint16_t>(total);

        if (d->squadId < SquadRegistry::kMaxSquads) continue;

        // 未所属の機を空きスロットのある小隊へ入れる。欠員から先に埋めるので、
        // 同じスロットに2機が重なることはない。
        bool placed = false;
        for (uint8_t s = 0; s < SquadRegistry::kMaxSquads && !placed; ++s)
        {
            for (uint8_t slot = 0; slot < SquadRegistry::kSquadSize; ++slot)
            {
                if (usedSlots[s] & (1u << slot)) continue;

                d->squadId     = s;
                d->slotInSquad = slot;
                usedSlots[s]   = static_cast<uint8_t>(usedSlots[s] | (1u << slot));
                placed         = true;
                break;
            }
        }
        // 満杯（32小隊×5機＝160機超）なら無所属のまま。その機は大技で使われる。
    }
}
 

// 最初のボスを探す。
EntityID DroneManagerSystem::FindBoss() const
{
    auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
    for (auto b : bossView) return b;   // 最初の1体
    return InvalidEntityID;
}



int DroneManagerSystem::SpawnDrones(int count)
{
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
            // 生成時の初期陣形はボスの頭上高くで旋回する HighOrbit。
            d->currentFormation = DroneFormationType::HighOrbit;
            d->orbitRadius      = 8.0f;
            d->hasReachedSlot   = false;   // スポーン直後は必ず MoveToSlot から開始

            // 揺れの位相を個体ごとに散らす。EntityID を撹拌してから使うのは、連番の
            // まま等間隔に配ると、遅いホバーはばらけても速い震えの側では位相が一周ぶん
            // 進んで再び揃ってしまうため。
            uint32_t phase = static_cast<uint32_t>(droneId) * 2654435761u;
            phase ^= phase >> 15;
            d->hoverTimeOffset = static_cast<float>(phase % 65536u) * (kHoverPhasePeriod / 65536.0f);
        }
        if (auto *tm = m_world->GetComponent<TimeStateComponent>(droneId))
        {
            tm->group = TimeGroup::Enemy;
        }

        ++spawned;
    }

    // Prefab 生成の遅延反映。この後の View で新規ドローンを拾えるようにする。
    m_world->ScrutinyAndApply();

    // 既存＋新規をまとめて連番にしないと円が歪む。
    ReindexAllDrones();

    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] %d 機スポーン（合計 %d）",
        spawned, GetAliveCount());
    return spawned;
}


// 先頭から count 機破壊する。
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

            // 突撃に必要な最低限のデータを補う。
            if (state == DroneState::FireCharge)
            {
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


// OnGui：生成・破壊・カウント確認のデバッグUI。
void DroneManagerSystem::OnGui()
{
    if (ImGui::CollapsingHeader("Drone Manager (Spawn / Count / Destroy)",
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();

        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "Auto Initial Spawn (シーン開始時)");
        ImGui::Checkbox("Enable Auto Spawn", &m_autoSpawnEnabled);
        ImGui::SliderInt("Initial Count", &m_initialSpawnCount, 1, 1000);
        ImGui::Text("Auto Spawn Done: %s", m_autoSpawnDone ? "yes" : "no");
        if (ImGui::Button("Re-arm Auto Spawn", ImVec2(160, 24)))
            m_autoSpawnDone = false;   // もう一度シーン開始時のように配備し直す
        ImGui::Separator();

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Spawn Settings");
        ImGui::SliderInt("Spawn Count", &m_spawnCount, 1, 100);

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

        // 攻撃パイプラインの可視化。BTのログには「いつ指示が出たか」しか出ないので、
        // 群れが指示に追いつけているのかが判断できない。
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "Attack Pipeline (指示と個体の追従)");

        const EntityID bossId = FindBoss();
        if (bossId != InvalidEntityID)
        {
            if (const auto *cmd = m_world->GetComponent<BossCommandComponent>(bossId))
            {
                const size_t f = static_cast<size_t>(cmd->requestFormation);
                ImGui::Text("ボスの指示 : %s   /   突撃信号 %s",
                            f < kDroneFormationSlotCount ? kDroneFormationNames[f] : "?",
                            cmd->droneAttackActive ? "ON" : "OFF");
            }
        }

        {
            int   total = 0, wants = 0, reached = 0, moving = 0;
            int   st[5] = {0, 0, 0, 0, 0};
            float sumProgress  = 0.0f;
            int   telegraphing = 0;
            float sumWindup    = 0.0f;

            auto droneView = m_world->View<DroneComponent>();
            for (auto e : droneView)
            {
                const auto *d = m_world->GetComponent<DroneComponent>(e);
                if (!d) continue;
                ++total;
                if (d->wantsAttack) ++wants;
                if (d->hasReachedSlot) ++reached;
                if (d->attackProgress > 0.01f) ++moving;
                sumProgress += d->attackProgress;
                if (d->windupTimer > 0.0f) ++telegraphing;
                sumWindup += d->windupTimer;
                const int s = static_cast<int>(d->currentState);
                if (s >= 0 && s < 5) ++st[s];
            }

            if (total > 0)
            {
                // 総数に届かないうちに突撃信号が立つ＝予備動作の待ち時間が足りていない。
                ImGui::Text("突撃したい機 %d / %d      持ち場に着いた機 %d / %d",
                            wants, total, reached, total);
                ImGui::Text("形が動いている機 %d      attackProgress 平均 %.2f",
                            moving, sumProgress / total);
                // 0 のまま突撃信号が立つなら、予告の Wait が短すぎて溜まる前に撃っている。
                ImGui::Text("予告で震えている機 %d      溜め 平均 %.2f 秒",
                            telegraphing, sumWindup / total);
                ImGui::Text("状態  Idle %d   Move %d   Fire %d   Stun %d   Return %d",
                            st[0], st[1], st[2], st[3], st[4]);
            }
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

        // 陣形攻撃テスト。BTを組まずに BossCommandComponent へ直接命令を書き、
        // DroneCommandSystem → DroneBrainSystem → Formation の流れを確認する。
        // ボスBTが毎フレーム陣形を指示していると上書きされるので、テスト時は外すこと。
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

        // 個別行動テスト。
        //   ①カミカゼ：出撃は一斉だが、飛び切った機から順に離脱して隊列へ戻る
        //   ②小隊突撃：5機編隊が接近 → 先頭がプレイヤーを追って突入
        //   ③硬直は切り払いを当てて確認する（Stunned → バラバラに復帰）
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.8f, 1.0f), "個別行動テスト");

        if (ImGui::Button("カミカゼ：構え", ImVec2(150, 26)))
            setBossCmd(DroneFormationType::KamikazeSwarm, false);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        if (ImGui::Button("カミカゼ：発射", ImVec2(150, 26)))
            setBossCmd(DroneFormationType::KamikazeSwarm, true);
        ImGui::PopStyleColor();

        if (ImGui::Button("小隊：接近", ImVec2(150, 26)))
            setBossCmd(DroneFormationType::SquadAssault, false);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        if (ImGui::Button("小隊：突入", ImVec2(150, 26)))
            setBossCmd(DroneFormationType::SquadAssault, true);
        ImGui::PopStyleColor();

        // 震え方は数字では決められないので実行中に見ながら合わせる。
        {
            if (!m_world->HasResource<DroneShiverTuning>())
                m_world->AddResource<DroneShiverTuning>();
            auto &shiver = m_world->GetResource<DroneShiverTuning>();

            if (ImGui::TreeNodeEx("予告の震え", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SliderFloat("振れ幅",   &shiver.amplitude, 0.0f,  1.0f, "%.2f m");
                ImGui::SliderFloat("速さ",     &shiver.frequency, 0.5f, 20.0f, "%.1f Hz");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("上げすぎるとノイズに見えて予告にならない。5〜8Hz が目安");
                ImGui::SliderFloat("溜め時間", &shiver.rampTime,  0.1f,  4.0f, "%.1f s");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("この秒数で振れ幅が最大になる。BT の Wait(予備動作) より短くすると、撃つ前に溜まり切る");
                ImGui::SliderFloat("ばらつき", &shiver.scatter,   0.0f,  1.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("0 にすると全機が同じ拍で揺れて「呼吸」に見える。1 でバラバラの震え");

                if (ImGui::Button("震えを既定値へ", ImVec2(140, 24)))
                    shiver = DroneShiverTuning{};

                ImGui::TreePop();
            }
        }

        // しなり具合は目で見ないと決まらないので実行中に変えられるようにする。
        if (m_world->HasResource<SquadRegistry>())
        {
            auto &tuning = m_world->GetResource<SquadRegistry>().tuning;
            if (ImGui::TreeNodeEx("小隊の調整", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "蛇のしなり");
                ImGui::SliderFloat("追従バネ",   &tuning.followGain,     1.0f, 30.0f, "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("間隔のずれを直す強さ。小さいほど胴体が伸び縮みして柔らかく見える");
                ImGui::SliderFloat("後席の鈍さ", &tuning.slotLagPerSlot, 0.0f, 0.25f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("1つ後ろの席ごとにバネを弱める割合。大きいほど尾が遅れて曲がる");

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "隊列の形");
                ImGui::SliderFloat("胴体の間隔", &tuning.chainGap, 1.0f, 10.0f, "%.1f m");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("前の機体との距離。小さいほど蛇が詰まって1本に見える");

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.5f, 1.0f), "突入");
                ImGui::SliderFloat("突入速度",   &tuning.strikeSpeed,      10.0f, 90.0f, "%.0f m/s");
                ImGui::SliderFloat("突入時間",   &tuning.strikeDuration,    0.5f,  5.0f, "%.1f s");
                ImGui::SliderFloat("再突入間隔", &tuning.approachMinTime,   0.0f,  6.0f, "%.1f s");
                ImGui::SliderFloat("旋回上限",   &tuning.maxTurnRateDeg,   10.0f, 360.0f, "%.0f deg/s");
                ImGui::SliderFloat("直線化距離", &tuning.terminalLockDist,  0.0f, 30.0f, "%.1f m");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("この距離まで詰めたら追尾をやめて直線になる。0 にすると完全追尾＝ジャスト回避が成立しない");

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "接近");
                ImGui::SliderFloat("接近半径", &tuning.approachRadius, 5.0f, 60.0f, "%.0f m");
                ImGui::SliderFloat("接近高度", &tuning.approachHeight, 0.0f, 30.0f, "%.0f m");

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.6f, 1.0f), "地面");
                ImGui::SliderFloat("最低高度", &tuning.minAltitude, 0.0f, 10.0f, "%.1f m");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("床からこれ以下へは沈まない。突入はプレイヤー（ほぼ床の高さ）を目指すので、ここを 0 にすると地面に埋まる");

                if (ImGui::Button("既定値に戻す", ImVec2(140, 24)))
                    tuning = SquadTuning{};

                ImGui::TreePop();
            }
        }

        // 小隊の中身（頭の位置とフェーズ）。
        if (m_world->HasResource<SquadRegistry>())
        {
            const auto &reg = m_world->GetResource<SquadRegistry>();
            int         shown = 0;
            for (uint8_t i = 0; i < SquadRegistry::kMaxSquads && shown < 6; ++i)
            {
                const auto &s = reg.squads[i];
                if (s.aliveCount == 0) continue;
                const char *phaseName = (s.phase == SquadPhase::Regroup)  ? "Regroup"
                                      : (s.phase == SquadPhase::Approach) ? "Approach"
                                                                          : "Strike";
                ImGui::Text("小隊%2d: %d機  %-8s  (%.0f, %.0f, %.0f)",
                            i, s.aliveCount, phaseName,
                            s.anchorPos.x, s.anchorPos.y, s.anchorPos.z);
                ++shown;
            }
        }

        ImGui::Unindent();
    }
}

// L02_Update で計数する（判断・陣形と同じステージ。順序は問わない）。
REGISTER_LOGIC_SYSTEM(DroneManagerSystem, Priority::LogicStage::L02_Update)
