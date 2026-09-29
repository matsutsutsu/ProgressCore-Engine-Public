#include "DroneManagerSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Formations_DataDriven.h" // 陣形テストの名前表示
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

static constexpr const char *kDronePrefab = "Assets/Prefabs/Enemy/Drone.json";

void DroneManagerSystem::Update(float /*dt*/)
{
    // ボスが揃い次第、初期ドローンを1回だけ配備する。
    TryAutoSpawnInitial();

    if (!m_world->HasResource<BattleContext>()) return;
    auto &battle = m_world->GetResource<BattleContext>();

    int alive = GetAliveCount();

    battle.droneAliveCount = alive;
}

void DroneManagerSystem::TryAutoSpawnInitial()
{
    if (!m_autoSpawnEnabled || m_autoSpawnDone) return;

    // ボスが未生成のフレームは何もせず次フレームに再試行する。
    if (FindBoss() == InvalidEntityID) return;

    int spawned = SpawnDrones(m_initialSpawnCount);
    if (spawned > 0)
    {
        m_autoSpawnDone = true; // 成功したらラッチして二度と自動生成しない
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
        // 満杯（32小隊×5機＝160機を超えた分）なら無所属のまま。その機は大技だけで使われる。
    }
}

EntityID DroneManagerSystem::FindBoss() const
{
    auto bossView = m_world->View<BossCommandComponent, TransformComponent>();
    for (auto b : bossView)
        return b; // 最初の1体
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
            d->hasReachedSlot   = false; // 生成直後は必ず MoveToTarget から始める

            // 揺れの位相を個体ごとにずらす。EntityID をハッシュしてから使うのは、連番の
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

void DroneManagerSystem::SetBossCommand(DroneFormationType formation, bool attack)
{
    if (auto *cmd = m_world->GetComponent<BossCommandComponent>(FindBoss()))
    {
        cmd->requestFormation  = formation;
        cmd->droneAttackActive = attack;
    }
}

const char *DroneManagerSystem::FormationDisplayName(DroneFormationType type) const
{
    // データ陣形は枠番号ではなくアセットの名前を出す。"[データ] 枠0" のままだと、
    // 枠に何を入れたかを覚えていないと選べない。
    if (auto *f = CCL::AI::Formation::FormationRegistry::Instance().Get(type))
    {
        if (auto *data = dynamic_cast<CCL::AI::Formation::Data::DataDrivenFormation *>(f))
            return data->GetAsset().name.c_str();
    }

    const size_t slot = static_cast<size_t>(type);
    return (slot < kDroneFormationSlotCount) ? kDroneFormationNames[slot] : "(不明)";
}

void DroneManagerSystem::DrawFormationTest()
{
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "陣形テスト");

    auto &registry = CCL::AI::Formation::FormationRegistry::Instance();

    if (ImGui::BeginCombo("陣形", FormationDisplayName(m_testFormation)))
    {
        for (size_t slot = 0; slot < kDroneFormationSlotCount; ++slot)
        {
            const auto type = static_cast<DroneFormationType>(slot);
            if (!registry.Has(type)) continue;

            const bool selected = (type == m_testFormation);
            if (ImGui::Selectable(FormationDisplayName(type), selected))
            {
                // 直前の選択を覚えておく。C++実装とデータ実装の見比べは往復が要るので、
                // 切り替えのたびにコンボを開き直していると形が崩れる前に間に合わない。
                m_testPrevFormation = m_testFormation;
                m_testFormation     = type;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("直前と入替", ImVec2(120, 0)))
    {
        std::swap(m_testFormation, m_testPrevFormation);

        // 攻撃信号は維持したまま形だけ差し替える。止めてしまうと進行度が0に戻り、
        // 「振り切った姿勢どうしの見比べ」ができない。
        if (auto *cmd = m_world->GetComponent<BossCommandComponent>(FindBoss()))
            SetBossCommand(m_testFormation, cmd->droneAttackActive);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("直前に選んだ「%s」と往復する。同じ絵になるかを見比べる用",
                          FormationDisplayName(m_testPrevFormation));

    if (ImGui::Button("1) 形を作る", ImVec2(180, 26)))
        SetBossCommand(m_testFormation, false);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
    if (ImGui::Button("2) 撃つ！", ImVec2(180, 26)))
        SetBossCommand(m_testFormation, true);
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("3) 周回へ戻す", ImVec2(140, 26)))
        SetBossCommand(DroneFormationType::OrbitCircle, false);

    // ボスBTが動いていると、次のフレームに requestFormation を上書きされて
    // 押しても戻る。原因が分かりにくいので画面に出しておく。
    ImGui::TextDisabled("注意: ボスBTが動いていると上書きされる。Behavior Tree の強制実行を解除しておく");
}

void DroneManagerSystem::SetAllDronesArrived(bool treatAsArrived)
{
    int  count = 0;
    auto view  = m_world->View<DroneComponent>();
    for (auto e : view)
    {
        if (auto *d = m_world->GetComponent<DroneComponent>(e))
        {
            d->hasReachedSlot = treatAsArrived;
            ++count;
        }
    }
    CCL_LOG_INFO(LogCategory::Game, "[DroneManager] 全 %d 機を %s 扱いにした",
                 count, treatAsArrived ? "到着済み" : "未到着(再接近)");
}

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
            m_autoSpawnDone = false; // もう一度シーン開始時のように配備し直す
        ImGui::Separator();

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Spawn Settings");
        ImGui::SliderInt("Spawn Count", &m_spawnCount, 1, 100);

        int alive = GetAliveCount();
        ImGui::Text("Current Active Drones: %d", alive);

        if (m_world->HasResource<BattleContext>())
        {
            auto &b = m_world->GetResource<BattleContext>();
            ImGui::Text("BattleContext.droneAliveCount: %d", b.droneAliveCount);
            // 構想上の段階分け。これを読む BT 条件はまだ無く、ドローンも減らないので
            //   実際の挙動は変わらない（初期配備100機なら常に "Final"）。
            const char *tactic =
                b.droneAliveCount <= 100 ? "Final" : b.droneAliveCount <= 400 ? "Aggressive"
                                                 : b.droneAliveCount <= 700   ? "Pressure"
                                                                              : "Control";
            ImGui::TextDisabled("→ 想定戦術(未接続): %s", tactic);
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
            int   st[5]        = {0, 0, 0, 0, 0};
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

        // currentState は判断層が毎フレーム決める。ここから触れるのは、
        //   判断層が入力として読む「到着したか」のラッチだけ。
        if (ImGui::Button("持ち場へ戻す (再接近)", ImVec2(170, 26)))
            SetAllDronesArrived(false);

        ImGui::SameLine();
        if (ImGui::Button("到着済みにする", ImVec2(140, 26)))
            SetAllDronesArrived(true);

        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "突撃させたいときは下の「カミカゼ：発射」を使うこと。\n"
                "状態を直接指定する操作は無い："
                "DroneBrainSystem が毎フレーム書き直すので効かない。");

        // 陣形攻撃テスト。BTを組まずに BossCommandComponent へ直接命令を書き、
        // DroneCommandSystem → DroneBrainSystem → Formation の流れを確認する。
        // ボスBTが毎フレーム陣形を指示していると上書きされるので、テスト時は外すこと。
        ImGui::Spacing();
        DrawFormationTest();

        // 個別行動テスト。
        //   1. 弾幕（KamikazeSwarm）: 出撃は一斉だが、飛び切った機から順に離脱して隊列へ戻る
        //   2. 小隊突撃: 5機編隊が接近 → 先頭がプレイヤーを追って突入
        //   3. 硬直は切り払いを当てて確認する（Stunned → 機体ごとの時間差で復帰）
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.8f, 1.0f), "個別行動テスト");

        if (ImGui::Button("カミカゼ：構え", ImVec2(150, 26)))
            SetBossCommand(DroneFormationType::KamikazeSwarm, false);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        if (ImGui::Button("カミカゼ：発射", ImVec2(150, 26)))
            SetBossCommand(DroneFormationType::KamikazeSwarm, true);
        ImGui::PopStyleColor();

        if (ImGui::Button("小隊：接近", ImVec2(150, 26)))
            SetBossCommand(DroneFormationType::SquadAssault, false);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.3f, 0.2f, 1.0f));
        if (ImGui::Button("小隊：突入", ImVec2(150, 26)))
            SetBossCommand(DroneFormationType::SquadAssault, true);
        ImGui::PopStyleColor();

        // 震え方は数字では決められないので実行中に見ながら合わせる。
        {
            if (!m_world->HasResource<DroneShiverTuning>())
                m_world->AddResource<DroneShiverTuning>();
            auto &shiver = m_world->GetResource<DroneShiverTuning>();

            if (ImGui::TreeNodeEx("予告の震え", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SliderFloat("振れ幅", &shiver.amplitude, 0.0f, 1.0f, "%.2f m");
                ImGui::SliderFloat("速さ", &shiver.frequency, 0.5f, 20.0f, "%.1f Hz");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("上げすぎるとノイズに見えて予告にならない。5〜8Hz が目安");
                ImGui::SliderFloat("溜め時間", &shiver.rampTime, 0.1f, 4.0f, "%.1f s");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("この秒数で振れ幅が最大になる。BT の Wait(予備動作) より短くすると、撃つ前に溜まり切る");
                ImGui::SliderFloat("ばらつき", &shiver.scatter, 0.0f, 1.0f, "%.2f");
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
                ImGui::SliderFloat("追従バネ", &tuning.followGain, 1.0f, 30.0f, "%.1f");
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
                ImGui::SliderFloat("突入速度", &tuning.strikeSpeed, 10.0f, 90.0f, "%.0f m/s");
                ImGui::SliderFloat("突入時間", &tuning.strikeDuration, 0.5f, 5.0f, "%.1f s");
                ImGui::SliderFloat("再突入間隔", &tuning.approachMinTime, 0.0f, 6.0f, "%.1f s");
                ImGui::SliderFloat("旋回上限", &tuning.maxTurnRateDeg, 10.0f, 360.0f, "%.0f deg/s");
                ImGui::SliderFloat("直線化距離", &tuning.terminalLockDist, 0.0f, 30.0f, "%.1f m");
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

        // 小隊の中身（アンカーの位置とフェーズ）。
        if (m_world->HasResource<SquadRegistry>())
        {
            const auto &reg   = m_world->GetResource<SquadRegistry>();
            int         shown = 0;
            for (uint8_t i = 0; i < SquadRegistry::kMaxSquads && shown < 6; ++i)
            {
                const auto &s = reg.squads[i];
                if (s.aliveCount == 0) continue;
                const char *phaseName = (s.phase == SquadPhase::Regroup)    ? "Regroup"
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

// 陣形（DroneFormationSystem）と同じ L02_Update。前後は GetRunAfter で決める。
REGISTER_LOGIC_SYSTEM(DroneManagerSystem, Priority::LogicStage::L02_Update)
