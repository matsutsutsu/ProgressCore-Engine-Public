#include "Editor/Windows/AI/BossBTDebugWindow.h"

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Engine/ECS/Core/CCL_World.h"

#include <cstdio>

#include <imgui.h>

namespace
{
    const ImVec4 kForcedColor(1.00f, 0.55f, 0.20f, 1.0f); // 強制実行中の行

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

void BossBTDebugWindow::DrawContents(EditorContext &context)
{
    const BossHandles boss = FindBoss(context);
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



void BossBTDebugWindow::DrawStatus(const BossHandles &boss)
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



void BossBTDebugWindow::DrawNode(const BTAsset &asset, BTNodeID nodeId, const BossHandles &boss,
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
// 補助
// ---------------------------------------------------------------------------

BossBTDebugWindow::BossHandles BossBTDebugWindow::FindBoss(EditorContext &context) const
{
    BossHandles handles;
    if (!context.world)
    {
        return handles;
    }

    for (auto boss : context.world->View<BossCommandComponent>())
    {
        handles.bt  = context.world->GetComponent<BehaviorTreeComponent>(boss);
        handles.cmd = context.world->GetComponent<BossCommandComponent>(boss);
        break;
    }
    return handles;
}



std::string BossBTDebugWindow::MakeLabel(const BTAsset &asset, BTNodeID nodeId, const std::string &inheritedName) const
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



const char *BossBTDebugWindow::FirstActionName(const BTAsset &asset, BTNodeID nodeId) const
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



void BossBTDebugWindow::SetForced(const BossHandles &boss, int nodeId)
{
    boss.bt->debugForcedNodeId = nodeId;

    // 前の技の「今撃て」信号が残ると、切り替えた後もドローンが攻撃を続ける
    if (boss.cmd)
    {
        boss.cmd->droneAttackActive = false;
    }
}
