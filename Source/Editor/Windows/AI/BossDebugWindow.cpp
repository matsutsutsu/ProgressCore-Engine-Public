#include "Editor/Windows/AI/BossDebugWindow.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Game/Logics/Character/Boss/Systems/BossActionSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Logics/AI/Debug/AIDebugOverlaySystem.h"
#include "Game/Logics/AI/Debug/AIDebugSpatialSystem.h"
#include "Game/Logics/AI/Debug/DroneDebugDrawSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"

#include <cstdio>

#include <imgui.h>

namespace
{
    const ImVec4 kForcedColor(1.00f, 0.55f, 0.20f, 1.0f); // 強制実行中の行

    // 「第2形態へ（演出あり）」で、HP をしきい値よりこの幅だけ下に置く（最大 HP に対する %）
    // ちょうどしきい値にすると、割り算の誤差で「しきい値以下」にならず移らないことがある
    constexpr float kBelowLinePercent = 0.1f;

    // System の OnGui を、折りたためる見出しの下に出す
    // 設定の持ち主は System のまま。System Manager の見出しと同じ中身がここにも出る
    // デバッグ描画の System はエディタのビルドにしか登録されないので、見つからなければその旨を出す
    template <class TSystem>
    void DrawSystemSection(CCL::ECS::SystemManager &systems, const char *title)
    {
        if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) return;

        TSystem *system = systems.GetSystem<TSystem>();
        if (!system)
        {
            ImGui::TextDisabled("この System は登録されていません");
            return;
        }

        // 見出しごとに ID を分ける。各 System の OnGui は同じラベル（"Font Size" など）を使うことがある
        ImGui::PushID(title);
        system->OnGui();
        ImGui::PopID();
    }

    // ノードの種類の表示名（アセットに保存する文字列と同じ）
    const char *NodeTypeName(BTNodeType type)
    {
        switch (type)
        {
        case BTNodeType::Selector: return "Selector";
        case BTNodeType::Sequence: return "Sequence";
        case BTNodeType::Condition: return "Condition";
        case BTNodeType::Action: return "Action";
        case BTNodeType::Decorator: return "Decorator";
        case BTNodeType::WeightedSelector: return "WeightedSelector";
        case BTNodeType::Parallel: return "Parallel";
        case BTNodeType::UtilitySelector: return "UtilitySelector";
        default: return "Unknown";
        }
    }



    // param を小数第1位までの文字列にする
    std::string FormatParam(float value)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", value);
        return buf;
    }
} // namespace

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void BossDebugWindow::DrawContents(EditorContext &context)
{
    const BossHandles boss = FindBoss(context);

    if (!ImGui::BeginTabBar("##BossDebugTabs")) return;

    if (ImGui::BeginTabItem("AI"))
    {
        DrawAiTab(boss);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("HP・形態"))
    {
        DrawHealthTab(boss);
        ImGui::EndTabItem();
    }

    // 表示の設定はボスがいなくても変えられる（System の値なので）
    if (ImGui::BeginTabItem("表示"))
    {
        DrawDisplayTab(context);
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}



// ---------------------------------------------------------------------------
// [AI]
// ---------------------------------------------------------------------------

void BossDebugWindow::DrawAiTab(const BossHandles &boss)
{
    if (!boss.bt)
    {
        ImGui::TextDisabled("ボスがいません（ボス戦のシーンで開いてください）");
        return;
    }
    if (!boss.bt->sharedAsset || boss.bt->sharedAsset->nodes.empty())
    {
        ImGui::TextDisabled("BT が読み込まれていません");
        return;
    }

    DrawStatus(boss);
    ImGui::Separator();

    ImGui::Checkbox("条件も表示", &m_showConditions);
    ImGui::SameLine();
    ImGui::TextDisabled("（「実行」でその枝だけを繰り返す。条件とクールダウンは無視）");

    // 「実行」ボタンとツリーを別の列に分ける。同じ列だと、親から引く線が子の行のボタンを貫く
    // 字下げはツリーの列にだけかけ、ボタンは縦一列にそろえる。ScrollY でツリーだけをスクロールさせる
    if (ImGui::BeginTable("##BossBT", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter))
    {
        const float buttonWidth = ImGui::CalcTextSize("解除").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::TableSetupColumn("##run", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable, buttonWidth);
        ImGui::TableSetupColumn("##tree", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable);

        DrawNode(*boss.bt->sharedAsset, 0, boss, std::string(), 0);

        ImGui::EndTable();
    }
}



void BossDebugWindow::DrawStatus(const BossHandles &boss)
{
    BehaviorTreeComponent &bt = *boss.bt;

    // --- 止める ---

    bool paused = bt.debugPaused;
    if (ImGui::Checkbox("AIを止める", &paused))
    {
        bt.debugPaused = paused;
    }

    // --- 今の状態 ---

    ImGui::SameLine();
    if (bt.debugForcedNodeId >= 0)
    {
        const std::string label = MakeLabel(*bt.sharedAsset, static_cast<BTNodeID>(bt.debugForcedNodeId), std::string());
        ImGui::TextColored(kForcedColor, "強制実行中: %s", label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("通常へ戻す"))
        {
            SetForced(boss, -1);
        }
    }
    else if (bt.debugPaused)
    {
        ImGui::TextUnformatted("停止中");
    }
    else
    {
        ImGui::TextDisabled("通常");
    }
}



void BossDebugWindow::DrawNode(const BTAsset &asset, BTNodeID nodeId, const BossHandles &boss,
                                 const std::string &inheritedName, int depth)
{
    const BTNode &node = asset.nodes[nodeId];

    // --- 省く行 ---

    // 子が1つの Decorator（クールダウン・採点の包み）は行を作らず、子の行にまとめる
    // 包みの行があると技1つで階層が1段深くなり、木が読みにくくなる
    if (node.type == BTNodeType::Decorator && node.children.size() == 1)
    {
        const bool        hasName = nodeId < asset.nodeNames.size() && !asset.nodeNames[nodeId].empty();
        const std::string name    = hasName ? asset.nodeNames[nodeId] : inheritedName;
        DrawNode(asset, node.children[0], boss, name, depth);
        return;
    }

    // 条件は単独で実行しても意味がないので、ボタンを付けずに出す
    if (node.type == BTNodeType::Condition)
    {
        if (m_showConditions)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(static_cast<int>(nodeId));
            ImGui::TreeNodeEx("##cond", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen, "%s",
                              MakeLabel(asset, nodeId, inheritedName).c_str());
            ImGui::PopID();
        }
        return;
    }

    ImGui::TableNextRow();
    ImGui::PushID(static_cast<int>(nodeId));

    // --- 実行ボタン ---

    ImGui::TableSetColumnIndex(0);
    const bool isForced = (boss.bt->debugForcedNodeId == static_cast<int>(nodeId));
    if (ImGui::SmallButton(isForced ? "解除" : "実行"))
    {
        SetForced(boss, isForced ? -1 : static_cast<int>(nodeId));
    }

    // --- 行 ---

    ImGui::TableSetColumnIndex(1);

    if (isForced)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kForcedColor);
    }

    // 最初は上の2段だけ開く。全部開くと技の一覧が画面に収まらない
    // DrawLinesToNodes で親から子へ線を引く（BT エディタのアウトラインと同じ）
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DrawLinesToNodes;
    if (depth < 2)
    {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    if (node.children.empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    const bool open = ImGui::TreeNodeEx("##node", flags, "%s", MakeLabel(asset, nodeId, inheritedName).c_str());

    if (isForced)
    {
        ImGui::PopStyleColor();
    }

    // --- 子 ---

    if (open && !node.children.empty())
    {
        for (BTNodeID child : node.children)
        {
            DrawNode(asset, child, boss, std::string(), depth + 1);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}



// ---------------------------------------------------------------------------
// [HP・形態]
// ---------------------------------------------------------------------------

void BossDebugWindow::DrawHealthTab(const BossHandles &boss)
{
    // 処理フロー:
    // 1. 今の状態を出す
    // 2. HP を減らさない
    // 3. HP を動かすボタン（とどめの手前・全回復）
    // 4. 第2形態へ飛ぶボタン

    if (!boss.health || !boss.phase)
    {
        ImGui::TextDisabled("ボスがいません（ボス戦のシーンで開いてください）");
        return;
    }
    HealthComponent    &health = *boss.health;
    BossPhaseComponent &phase  = *boss.phase;

    // ---------------------------------------------------------
    // 1. 状態
    // ---------------------------------------------------------
    const float hpPercent = (health.maxHealth > 0.0f) ? health.currentHealth / health.maxHealth * 100.0f : 0.0f;
    const bool  raging    = boss.cmd && boss.cmd->raging;
    const bool  exhausted = boss.action && boss.action->currentState == BossActionState::Exhausted;

    ImGui::Text("形態: %s", ToString(phase.phase));
    ImGui::Text("HP: %.0f / %.0f（%.1f%%）", health.currentHealth, health.maxHealth, hpPercent);
    ImGui::Text("とどめ以外の下限: %.0f", health.finisherFloor);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("とどめ区間の命中以外では、HP がこれより下がらない。0 なら下限なし");
    if (boss.bt) ImGui::Text("被弾の蓄積: %.1f%%", boss.bt->blackboard.accumulatedDamageRatio);
    ImGui::Text("怒り: %s　疲れ: %s", raging ? "中" : "-", exhausted ? "中" : "-");

    ImGui::Separator();

    // ---------------------------------------------------------
    // 2. HP を減らさない
    // ---------------------------------------------------------
    ImGui::Checkbox("HP を減らさない", &health.debugNoDamage);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("当たった演出と被弾の蓄積（怒り）は普段どおり出る\n"
                          "とどめでも HP が減らないので、ON の間は形態が進まない");

    // ---------------------------------------------------------
    // 3. HP を動かす
    // ---------------------------------------------------------
    if (ImGui::Button("全回復")) health.currentHealth = health.maxHealth;
    ImGui::SameLine();

    // 下限は第1形態なら移行の線のすぐ上、最後の形態なら 1（BossPhaseSystem が毎フレーム書く）
    // 下限ちょうどにすれば、次のとどめの命中でちょうど線を越える
    const bool  toPhase2   = (phase.phase == BossPhase::Combat) && !phase.phase2AssetPath.empty();
    const char *floorLabel = toPhase2 ? "とどめ1発で第2形態へ" : "とどめ1発で撃破";
    ImGui::BeginDisabled(health.finisherFloor <= 0.0f);
    if (ImGui::Button(floorLabel)) health.currentHealth = health.finisherFloor;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("HP を「とどめ以外の下限」にする。次のとどめ区間の命中で線を越える\n"
                          "移行演出中・死亡後など、下限がない間は押せない");

    // ---------------------------------------------------------
    // 4. 第2形態へ飛ぶ
    // ---------------------------------------------------------

    // 飛べるのは、BossPhaseSystem が移行演出へ入るのと同じ条件のときだけ
    const bool canEnterPhase2 = toPhase2 && health.maxHealth > 0.0f;
    ImGui::BeginDisabled(!canEnterPhase2);

    if (ImGui::Button("第2形態へ（演出あり）"))
        health.currentHealth = health.maxHealth * (phase.phase2HealthThreshold - kBelowLinePercent) / 100.0f;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("HP をしきい値の少し下にする。次のフレームに、普段どおり移行演出（スロー → ムービー）が始まる");
    ImGui::SameLine();

    // 形態を変えたときの処理（BT の差し替えなど）は BossPhaseSystem にあるので、要求だけを立てる
    if (ImGui::Button("第2形態から（演出なし）")) phase.debugSkipToPhase2 = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("移行演出とムービーを飛ばし、第2形態の BT から始める。HP はしきい値まで下がる");

    ImGui::EndDisabled();
    if (!canEnterPhase2) ImGui::TextDisabled("第2形態へ飛べるのは、第1形態の間だけ（第2形態の BT が未設定なら飛べない）");
}



// ---------------------------------------------------------------------------
// [表示]
// ---------------------------------------------------------------------------

void BossDebugWindow::DrawDisplayTab(EditorContext &context)
{
    if (!context.systemManager)
    {
        ImGui::TextDisabled("SystemManager がありません");
        return;
    }
    CCL::ECS::SystemManager &systems = *context.systemManager;

    // 設定の持ち主は各 System。System Manager の見出しと同じ OnGui を、ここにまとめて出す
    DrawSystemSection<AIDebugOverlaySystem>(systems, "頭上の文字・技選択のパネル");
    DrawSystemSection<AIDebugSpatialSystem>(systems, "地面の帯");
    DrawSystemSection<DroneDebugDrawSystem>(systems, "ドローン");
    DrawSystemSection<BossActionSystem>(systems, "本体（位置のワープなど）");
}



// ---------------------------------------------------------------------------
// 補助
// ---------------------------------------------------------------------------

BossDebugWindow::BossHandles BossDebugWindow::FindBoss(EditorContext &context) const
{
    BossHandles handles;
    if (!context.world) return handles;

    for (auto boss : context.world->View<BossCommandComponent>())
    {
        handles.bt     = context.world->GetComponent<BehaviorTreeComponent>(boss);
        handles.cmd    = context.world->GetComponent<BossCommandComponent>(boss);
        handles.phase  = context.world->GetComponent<BossPhaseComponent>(boss);
        handles.action = context.world->GetComponent<BossActionComponent>(boss);
        handles.health = context.world->GetComponent<HealthComponent>(boss);
        break;
    }
    return handles;
}



std::string BossDebugWindow::MakeLabel(const BTAsset &asset, BTNodeID nodeId, const std::string &inheritedName) const
{
    const BTNode &node = asset.nodes[nodeId];

    std::string name = (nodeId < asset.nodeNames.size()) ? asset.nodeNames[nodeId] : std::string();
    if (name.empty())
    {
        name = inheritedName;
    }

    // --- 行動・条件：表示名と param ---

    if (node.type == BTNodeType::Action || node.type == BTNodeType::Condition)
    {
        const bool         isAction = (node.type == BTNodeType::Action);
        const ActionEntry *entry    = isAction ? FindActionEntry(node.actionOrConditionId)
                                               : FindConditionEntry(node.actionOrConditionId);

        std::string label = isAction ? "" : "条件: ";
        label += entry ? entry->name : ("id=" + std::to_string(node.actionOrConditionId));

        const char *paramLabel = isAction ? GetActionParamLabel(node.actionOrConditionId)
                                          : GetConditionParamLabel(node.actionOrConditionId);
        if (paramLabel)
        {
            label += " (" + FormatParam(node.param) + ")";
        }
        return name.empty() ? label : name + " : " + label;
    }

    // --- 複合ノード：名前がなければ種類と中身の先頭の行動 ---

    if (!name.empty())
    {
        return name;
    }

    std::string label = NodeTypeName(node.type);
    if (const char *hint = FirstActionName(asset, nodeId))
    {
        label += std::string(" [") + hint + "]";
    }
    return label;
}



const char *BossDebugWindow::FirstActionName(const BTAsset &asset, BTNodeID nodeId) const
{
    const BTNode &node = asset.nodes[nodeId];
    if (node.type == BTNodeType::Action)
    {
        const ActionEntry *entry = FindActionEntry(node.actionOrConditionId);
        return entry ? entry->name : nullptr;
    }

    for (BTNodeID child : node.children)
    {
        if (const char *name = FirstActionName(asset, child))
        {
            return name;
        }
    }
    return nullptr;
}



void BossDebugWindow::SetForced(const BossHandles &boss, int nodeId)
{
    boss.bt->debugForcedNodeId = nodeId;

    // 前の技の「今撃て」信号が残ると、切り替えた後もドローンが攻撃を続ける
    if (boss.cmd)
    {
        boss.cmd->droneAttackActive = false;
    }
}
