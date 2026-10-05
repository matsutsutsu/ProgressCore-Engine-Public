#include "DroneManagerSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/Drone/Components/DroneTuning.h" // 予告の震えの調整 UI
#include "Game/Logics/AI/Drone/Squad/SquadState.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/Drone/Formation/Data/DataDrivenFormation.h" // 陣形テストの名前表示
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
    // ボスが揃い次第、最初にドローンを1回だけ配備する。
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
        CCL_LOG_INFO(LogCategory::Game, "[DroneManager] シーン開始：初期ドローン %d 機を自動配備", spawned);
    }
}

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

    // 1小隊は8機以下である前提でビットマスクを使う（空きスロット探索用）。
    static_assert(SquadRegistry::kSquadSize <= 8, "小隊の人数が uint8_t のビット数を超えている");

    // 使用中のスロットを小隊ごとに集める。

    // 所属は一度決めたら維持する。
    // 振り直すと生き残った機の所属まで入れ替わり、同じ部隊で組んでいるように見えない。
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

    // 全機に連番を振り、未所属の機だけを小隊へ入れる。
    uint16_t idx  = 0;
    auto     view = m_world->View<DroneComponent>();
    for (auto e : view)
    {
        auto *d = m_world->GetComponent<DroneComponent>(e);
        if (!d) continue;

        d->localIndex  = idx++;
        d->totalDrones = static_cast<uint16_t>(total);

        if (d->squadId < SquadRegistry::kMaxSquads) continue;

        // 未所属の機を、空きスロットのある小隊へ入れる。
        // 空いている席から先に埋めるので、同じスロットに2機が重なることはない。
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
    // 処理フロー:
    // 1. ボスの位置を取る
    // 2. プレハブから生成し、初期値を入れる
    // 3. 生成を World へ反映する
    // 4. 既存の機と合わせて番号を振り直す

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

    // --- 反映 ---

    // Prefab の生成は遅延反映される。この後の View で新規ドローンを拾えるよう、ここで反映する。
    m_world->ScrutinyAndApply();

    // --- 番号の振り直し ---

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
    // データ陣形は枠番号ではなくアセットの名前を出す。
    // "[空き枠] 枠0" のままだと、枠に何を入れたかを覚えていないと選べない。
    if (auto *f = CCL::AI::Formation::FormationRegistry::Instance().Get(type))
    {
        if (auto *data = dynamic_cast<CCL::AI::Formation::Data::DataDrivenFormation *>(f))
            return data->GetAsset().name.c_str();
    }

    const size_t slot = static_cast<size_t>(type);
    return (slot < kDroneFormationSlotCount) ? kDroneFormationNames[slot] : "(不明)";
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

// ============================================================
// デバッグ UI
// ============================================================

namespace
{
    constexpr ImVec4 kAttackButtonColor  = ImVec4(0.9f, 0.3f, 0.2f, 1.0f); // 攻撃を出すボタン
    constexpr ImVec4 kDestroyButtonColor = ImVec4(0.8f, 0.2f, 0.2f, 1.0f); // 全機を消すボタン

    // 色付きのボタン。Push と Pop の対応を呼び出し側に書かせないためにまとめる。
    bool ColoredButton(const char *label, const ImVec2 &size, const ImVec4 &color)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor();
        return pressed;
    }

    // 直前の項目にマウスを乗せている間だけ説明を出す。
    void HoverTooltip(const char *text)
    {
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text);
    }

    /**
     * @brief 生存数から構想上の戦術名を返す。表示するだけで、実際の挙動は変わらない。
     *
     * これを読む BT の条件はまだ無く、ドローンも減らない（初期配備120機なら常に "Aggressive"）。
     *
     * @param droneAliveCount 生存しているドローンの数。
     * @return 戦術名（文字列リテラル）。
     */
    const char *PlannedTacticName(int droneAliveCount)
    {
        if (droneAliveCount <= 100) return "Final";
        if (droneAliveCount <= 400) return "Aggressive";
        if (droneAliveCount <= 700) return "Pressure";
        return "Control";
    }

    const char *SquadPhaseName(SquadPhase phase)
    {
        switch (phase)
        {
        case SquadPhase::Regroup: return "Regroup";
        case SquadPhase::Approach: return "Approach";
        default: return "Chase";
        }
    }

    // 攻撃パイプライン表示のための、全ドローンの集計。
    struct DronePipelineStats
    {
        int   total        = 0;
        int   wants        = 0;
        int   reached      = 0;
        int   moving       = 0;
        int   telegraphing = 0;
        float sumProgress  = 0.0f;
        float sumWindup    = 0.0f;
        int   states[5]    = {}; // DroneState ごとの機数（Idle / MoveToTarget / FireCharge / Stunned / Returning）
    };

    DronePipelineStats CountPipelineStats(World &world)
    {
        DronePipelineStats stats;

        auto droneView = world.View<DroneComponent>();
        for (auto e : droneView)
        {
            const auto *d = world.GetComponent<DroneComponent>(e);
            if (!d) continue;

            ++stats.total;
            if (d->wantsAttack) ++stats.wants;
            if (d->hasReachedSlot) ++stats.reached;
            if (d->attackProgress > 0.01f) ++stats.moving;
            if (d->windupTimer > 0.0f) ++stats.telegraphing;
            stats.sumProgress += d->attackProgress;
            stats.sumWindup += d->windupTimer;

            const int s = static_cast<int>(d->currentState);
            if (s >= 0 && s < 5) ++stats.states[s];
        }
        return stats;
    }
} // namespace

void DroneManagerSystem::OnGui()
{
    if (!ImGui::CollapsingHeader("Drone Manager (Spawn / Count / Destroy)",
                                 ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::Indent();

    DrawAutoSpawn();
    DrawSpawnSettings();
    DrawAttackPipeline();
    DrawSpawnButtons();
    DrawArrivalDebug();
    DrawFormationTest();
    DrawIndividualTest();
    DrawShiverTuning();
    DrawSquadTuning();
    DrawSquadList();

    ImGui::Unindent();
}

void DroneManagerSystem::DrawAutoSpawn()
{
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "Auto Initial Spawn (シーン開始時)");
    ImGui::Checkbox("Enable Auto Spawn", &m_autoSpawnEnabled);
    ImGui::SliderInt("Initial Count", &m_initialSpawnCount, 1, 1000);
    ImGui::Text("Auto Spawn Done: %s", m_autoSpawnDone ? "yes" : "no");
    if (ImGui::Button("Re-arm Auto Spawn", ImVec2(160, 24)))
        m_autoSpawnDone = false; // もう一度シーン開始時のように配備し直す
    ImGui::Separator();
}

void DroneManagerSystem::DrawSpawnSettings()
{
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Spawn Settings");
    ImGui::SliderInt("Spawn Count", &m_spawnCount, 1, 100);

    ImGui::Text("Current Active Drones: %d", GetAliveCount());

    if (m_world->HasResource<BattleContext>())
    {
        const auto &b = m_world->GetResource<BattleContext>();
        ImGui::Text("BattleContext.droneAliveCount: %d", b.droneAliveCount);
        ImGui::TextDisabled("→ 想定戦術(未接続): %s", PlannedTacticName(b.droneAliveCount));
    }
}

void DroneManagerSystem::DrawAttackPipeline()
{
    // 指示と個体の追従を並べて出す。
    // BT のログには「いつ指示が出たか」しか出ないので、群れが指示に追いつけているのかが判断できない。
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "Attack Pipeline (指示と個体の追従)");

    if (const auto *cmd = m_world->GetComponent<BossCommandComponent>(FindBoss()))
    {
        const size_t f = static_cast<size_t>(cmd->requestFormation);
        ImGui::Text("ボスの指示 : %s   /   突撃信号 %s",
                    f < kDroneFormationSlotCount ? kDroneFormationNames[f] : "?",
                    cmd->droneAttackActive ? "ON" : "OFF");
    }

    const DronePipelineStats st = CountPipelineStats(*m_world);
    if (st.total == 0) return;

    // 総数に届かないうちに突撃信号が立つ＝予備動作の待ち時間が足りていない。
    ImGui::Text("突撃したい機 %d / %d      持ち場に着いた機 %d / %d",
                st.wants, st.total, st.reached, st.total);
    ImGui::Text("形が動いている機 %d      attackProgress 平均 %.2f",
                st.moving, st.sumProgress / st.total);

    // 0 のまま突撃信号が立つなら、予告の Wait が短すぎて溜まる前に撃っている。
    ImGui::Text("予告で震えている機 %d      溜め 平均 %.2f 秒",
                st.telegraphing, st.sumWindup / st.total);
    ImGui::Text("状態  Idle %d   Move %d   Fire %d   Stun %d   Return %d",
                st.states[0], st.states[1], st.states[2], st.states[3], st.states[4]);
}

void DroneManagerSystem::DrawSpawnButtons()
{
    ImGui::Spacing();

    if (ImGui::Button("Spawn Drones", ImVec2(120, 30)))
        SpawnDrones(m_spawnCount);

    ImGui::SameLine();
    if (ImGui::Button("Destroy 50", ImVec2(100, 30)))
        DestroyDrones(50);

    ImGui::SameLine();
    if (ColoredButton("Destroy All", ImVec2(100, 30), kDestroyButtonColor))
        DestroyAll();
}

void DroneManagerSystem::DrawArrivalDebug()
{
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "State Debug (動作確認用)");

    // currentState は判断層が毎フレーム決めるので、ここから直接は変えられない。
    // ここから触れるのは、判断層が入力として読む「到着したか」のラッチだけ。
    if (ImGui::Button("持ち場へ戻す (再接近)", ImVec2(170, 26)))
        SetAllDronesArrived(false);

    ImGui::SameLine();
    if (ImGui::Button("到着済みにする", ImVec2(140, 26)))
        SetAllDronesArrived(true);

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    HoverTooltip("突撃させたいときは下の「弾幕：発射」を使うこと。\n"
                 "状態を直接指定する操作は無い："
                 "DroneBrainSystem が毎フレーム書き直すので効かない。");
}

void DroneManagerSystem::DrawFormationTest()
{
    // BT を組まずに BossCommandComponent へ直接命令を書き、
    // DroneCommandSystem → DroneBrainSystem → Formation の流れを確認する。
    ImGui::Spacing();
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
                // 直前の選択を覚えておく。
                // C++実装とデータ実装の見比べは往復が要るので、切り替えのたびにコンボを開き直していると
                // 形が崩れる前に間に合わない。
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

        // 攻撃信号は維持したまま形だけ差し替える。
        // 止めてしまうと進行度が0に戻り、「振り切った姿勢どうしの見比べ」ができない。
        if (auto *cmd = m_world->GetComponent<BossCommandComponent>(FindBoss()))
            SetBossCommand(m_testFormation, cmd->droneAttackActive);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("直前に選んだ「%s」と往復する。同じ絵になるかを見比べる用",
                          FormationDisplayName(m_testPrevFormation));

    if (ImGui::Button("1) 形を作る", ImVec2(180, 26)))
        SetBossCommand(m_testFormation, false);

    ImGui::SameLine();
    if (ColoredButton("2) 撃つ！", ImVec2(180, 26), kAttackButtonColor))
        SetBossCommand(m_testFormation, true);

    ImGui::SameLine();
    if (ImGui::Button("3) 周回へ戻す", ImVec2(140, 26)))
        SetBossCommand(DroneFormationType::LowOrbit, false);

    // ボスBTが動いていると、次のフレームに requestFormation を上書きされて押しても戻る。
    // 原因が分かりにくいので画面に出しておく。
    ImGui::TextDisabled("注意: ボスBTが動いていると上書きされる。Behavior Tree の強制実行を解除しておく");
}

void DroneManagerSystem::DrawIndividualTest()
{
    // 確かめる挙動:
    //   - 弾幕（Barrage）: 出撃は一斉だが、飛び切った機から順に離脱して隊列へ戻る
    //   - 小隊突撃: 5機編隊が扇へ展開 → 先頭がプレイヤーを追い続ける（合図を落とすまで）
    //   - 硬直: 切り払いを当てて確認する（Stunned → 機体ごとの時間差で復帰）。ボタンは無い
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.8f, 1.0f), "個別行動テスト");

    if (ImGui::Button("弾幕：構え", ImVec2(150, 26)))
        SetBossCommand(DroneFormationType::Barrage, false);
    ImGui::SameLine();
    if (ColoredButton("弾幕：発射", ImVec2(150, 26), kAttackButtonColor))
        SetBossCommand(DroneFormationType::Barrage, true);

    if (ImGui::Button("小隊：展開", ImVec2(150, 26)))
        SetBossCommand(DroneFormationType::SquadAssault, false);
    ImGui::SameLine();
    if (ColoredButton("小隊：追跡", ImVec2(150, 26), kAttackButtonColor))
        SetBossCommand(DroneFormationType::SquadAssault, true);
}

void DroneManagerSystem::DrawShiverTuning()
{
    // 震え方は数字では決められないので、実行中に見ながら合わせる。
    if (!m_world->HasResource<DroneShiverTuning>())
        m_world->AddResource<DroneShiverTuning>();
    auto &shiver = m_world->GetResource<DroneShiverTuning>();

    if (!ImGui::TreeNodeEx("予告の震え", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::SliderFloat("振れ幅", &shiver.amplitude, 0.0f, 1.0f, "%.2f m");

    ImGui::SliderFloat("速さ", &shiver.frequency, 0.5f, 20.0f, "%.1f Hz");
    HoverTooltip("上げすぎるとノイズに見えて予告にならない。5〜8Hz が目安");

    ImGui::SliderFloat("溜め時間", &shiver.rampTime, 0.1f, 4.0f, "%.1f s");
    HoverTooltip("この秒数で振れ幅が最大になる。BT の Wait(予備動作) より短くすると、撃つ前に溜まり切る");

    ImGui::SliderFloat("ばらつき", &shiver.scatter, 0.0f, 1.0f, "%.2f");
    HoverTooltip("0 にすると全機が同じ拍で揺れて「呼吸」に見える。1 でバラバラの震え");

    if (ImGui::Button("震えを既定値へ", ImVec2(140, 24)))
        shiver = DroneShiverTuning{};

    ImGui::TreePop();
}

void DroneManagerSystem::DrawSquadTuning()
{
    // しなり具合は目で見ないと決まらないので、実行中に変えられるようにする。
    if (!m_world->HasResource<SquadRegistry>()) return;
    auto &tuning = m_world->GetResource<SquadRegistry>().tuning;

    if (!ImGui::TreeNodeEx("小隊の調整", ImGuiTreeNodeFlags_DefaultOpen)) return;

    // --- 隊列のしなり ---

    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "隊列のしなり");
    ImGui::SliderFloat("追従バネ", &tuning.followGain, 1.0f, 30.0f, "%.1f");
    HoverTooltip("間隔のずれを直す強さ。小さいほど隊列が伸び縮みして柔らかく見える");
    ImGui::SliderFloat("後席の鈍さ", &tuning.slotLagPerSlot, 0.0f, 0.25f, "%.2f");
    HoverTooltip("1つ後ろの席ごとにバネを弱める割合。大きいほど最後尾が遅れて曲がる");

    // --- 隊列の形 ---

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "隊列の形");
    ImGui::SliderFloat("機体の間隔", &tuning.chainGap, 1.0f, 10.0f, "%.1f m");
    HoverTooltip("前の機体との距離。小さいほど隊列が詰まって1本に見える");

    // --- 突入 ---

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.5f, 1.0f), "追跡");
    ImGui::SliderFloat("速さ（走行比）", &tuning.chaseSpeedOverRun, 0.5f, 2.0f, "x%.2f");
    HoverTooltip("プレイヤーの走行速度に掛ける倍率。1 を超えると走って逃げても追いつかれる");
    ImGui::SliderFloat("速さのばらつき", &tuning.chaseSpeedSpread, 0.0f, 0.5f, "±%.2f");
    HoverTooltip("小隊ごとに上の倍率へ足す幅。一番遅い小隊でも 1 を超えるようにする");
    ImGui::SliderFloat("狙いの横ずれ", &tuning.aimSideOffset, 0.0f, 30.0f, "%.1f m");
    ImGui::SliderFloat("ずれを縮め始める距離", &tuning.aimFadeStartDist, 1.0f, 80.0f, "%.0f m");
    ImGui::SliderFloat("ずれが0になる距離", &tuning.aimFadeEndDist, 0.0f, 40.0f, "%.0f m");
    HoverTooltip("遠くでは狙いを左右へずらして包むように迫り、この2つの間で縮めて、内側ではプレイヤー本人を狙う");
    ImGui::SliderFloat("旋回時の最低速度", &tuning.turnSlowMinScale, 0.1f, 1.0f, "x%.2f");
    HoverTooltip("狙う方向とのずれが大きいときに落とす速さの下限。1 で減速しない（止まった相手の周りを回り続けやすい）");
    ImGui::SliderFloat("加減速", &tuning.chaseAccel, 5.0f, 300.0f, "%.0f m/s2");
    HoverTooltip("速さを変えられる上限。大きすぎると至近距離の切り返しにも急ブレーキで追いつき、避けられなくなる");
    ImGui::SliderFloat("旋回上限", &tuning.maxTurnRateDeg, 10.0f, 720.0f, "%.0f deg/s");
    HoverTooltip("回避はこの制限だけで成り立つ。上げすぎると切り返しても曲がって当たる");

    // --- 接近 ---

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "展開");
    ImGui::SliderFloat("展開半径", &tuning.approachRadius, 5.0f, 60.0f, "%.0f m");
    ImGui::SliderFloat("展開高度", &tuning.approachHeight, 0.0f, 30.0f, "%.0f m");
    ImGui::SliderFloat("扇の広さ", &tuning.approachArcDeg, 30.0f, 360.0f, "%.0f deg");
    HoverTooltip("小隊を並べる範囲。プレイヤーから見たボスの方向が中心。360 で全周を囲む");
    ImGui::SliderFloat("段の距離差", &tuning.layerRadiusSpread, 0.0f, 20.0f, "%.1f m");
    ImGui::SliderFloat("段の高さ差", &tuning.layerHeightSpread, 0.0f, 15.0f, "%.1f m");
    HoverTooltip("隣り合う小隊を手前・低い段と奥・高い段へ互い違いに振る幅。0 で全小隊が同じ円周に並ぶ");

    // --- 地面 ---

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.6f, 1.0f), "地面");
    ImGui::SliderFloat("最低高度", &tuning.minAltitude, 0.0f, 10.0f, "%.1f m");
    HoverTooltip("床からこれ以下へは沈まない。突入はプレイヤー（ほぼ床の高さ）を目指すので、ここを 0 にすると地面に埋まる");

    if (ImGui::Button("既定値に戻す", ImVec2(140, 24)))
        tuning = SquadTuning{};

    ImGui::TreePop();
}

void DroneManagerSystem::DrawSquadList()
{
    // 生きている小隊のアンカーの位置とフェーズを、先頭から6個まで出す。
    if (!m_world->HasResource<SquadRegistry>()) return;
    const auto &reg = m_world->GetResource<SquadRegistry>();

    int shown = 0;
    for (uint8_t i = 0; i < SquadRegistry::kMaxSquads && shown < 6; ++i)
    {
        const auto &s = reg.squads[i];
        if (s.aliveCount == 0) continue;

        ImGui::Text("小隊%2d: %d機  %-8s  (%.0f, %.0f, %.0f)",
                    i, s.aliveCount, SquadPhaseName(s.phase),
                    s.anchorPos.x, s.anchorPos.y, s.anchorPos.z);
        ++shown;
    }
}

// 陣形（DroneFormationSystem）と同じ L02_Update。前後は GetRunAfter で決める。
REGISTER_LOGIC_SYSTEM(DroneManagerSystem, Priority::LogicStage::L02_Update)
