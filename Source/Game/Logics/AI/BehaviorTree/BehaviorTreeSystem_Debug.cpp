#include "BehaviorTreeSystem.h"

#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/AI/BehaviorTree/BTUtilityScore.h" // 技選択の記録の大きさ（kUtilityTopCount）
#include "Engine/ECS/Core/CCL_World.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <imgui.h>
// 技を止めるときにだけ使う
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

using namespace CCL::ECS;
using namespace CCL::AI;
using namespace CCL::AI::BT;

// ---------------------------------------------------------------------------
// デバッグ UI
// ---------------------------------------------------------------------------

namespace
{
    // BTActionIds.h の辞書から表示名を逆引きする。見つからなければ nullptr
    const char *LookupEntryName(const ActionEntry *table, int count, ActionID id)
    {
        for (int i = 0; i < count; ++i)
        {
            if (table[i].id == id) return table[i].name;
        }
        return nullptr;
    }

    /**
     * @brief この枝で最初に見つかる Action の表示名を返す。
     *
     * Sequence や Selector は名前を持たないので、その枝が何をするかを中の先頭のアクションで示す。
     *
     * @param asset  木の構造。
     * @param nodeId 調べる枝の根のノード。
     * @return 行動の表示名（ActionRegistry の文字列）。枝に Action が無ければ nullptr。
     */
    const char *FirstActionNameInSubtree(const BTAsset &asset, BTNodeID nodeId)
    {
        const BTNode &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action)
        {
            return LookupEntryName(g_ActionRegistry, g_ActionRegistryCount, node.actionOrConditionId);
        }
        for (BTNodeID child : node.children)
        {
            if (const char *name = FirstActionNameInSubtree(asset, child)) return name;
        }
        return nullptr;
    }

    // param を小数第1位までの文字列にする（10.0 → "10.0"）
    std::string FormatParam(float v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        return buf;
    }

    // ノードの種類の表示名（アセットに保存する文字列と同じ）
    const char *NodeTypeName(BTNodeType type)
    {
        switch (type)
        {
        case BTNodeType::Selector:
            return "Selector";
        case BTNodeType::Sequence:
            return "Sequence";
        case BTNodeType::Condition:
            return "Condition";
        case BTNodeType::Action:
            return "Action";
        case BTNodeType::Decorator:
            return "Decorator";
        case BTNodeType::WeightedSelector:
            return "WeightedSelector";
        case BTNodeType::Parallel:
            return "Parallel";
        case BTNodeType::UtilitySelector:
            return "UtilitySelector";
        default:
            return "Unknown";
        }
    }
} // namespace

void BehaviorTreeSystem::DrawNodeRowRecursive(const BTAsset         &asset,
                                              BTNodeID               nodeId,
                                              BehaviorTreeComponent &btComp,
                                              EntityID               entity,
                                              bool                   parentIsUtility)
{
    // 処理フロー:
    // 1. 行のラベル（種類＋中身が分かる名前）を作る
    // 2. 実行状態の数字を足す
    // 3. 実行ボタンを描く
    // 4. 評価結果の色で行を描き、子の行へ降りる

    const BTNode &node = asset.nodes[nodeId];

    // ---------------------------------------------------------
    // 1. ラベル
    // ---------------------------------------------------------
    std::string label = NodeTypeName(node.type);
    if (node.type == BTNodeType::Action)
    {
        const char *name = LookupEntryName(g_ActionRegistry, g_ActionRegistryCount, node.actionOrConditionId);
        label += name ? std::string(" : ") + name
                      : std::string(" : id=") + std::to_string(node.actionOrConditionId);
        if (GetActionParamLabel(node.actionOrConditionId))
            label += " (" + FormatParam(node.param) + ")";
    }
    else if (node.type == BTNodeType::Condition)
    {
        const char *name = LookupEntryName(g_ConditionRegistry, g_ConditionRegistryCount, node.actionOrConditionId);
        label += name ? std::string(" : ") + name
                      : std::string(" : id=") + std::to_string(node.actionOrConditionId);
        // param を出さないと「距離が N m 未満」が並ぶだけで何mの判定か分からない。
        if (GetConditionParamLabel(node.actionOrConditionId))
            label += " (" + FormatParam(node.param) + ")";
    }
    else if (const char *hint = FirstActionNameInSubtree(asset, nodeId))
    {
        label += std::string(" [") + hint + "]";
    }

    // ---------------------------------------------------------
    // 2. 実行状態の数字
    // ---------------------------------------------------------

    // 色だけでは「なぜこのノードから進まないのか」が分からない。
    //   行動 … 残り時間。0.00 なら待ち終わっている、-1 は未実行
    //   複合 … 今どの子で止まっているか（再開位置）。変わらなければその子が Running のまま
    //   Cooldown … 残りクールダウン。>0 の間はこの枝ごと Failure
    char buf[64];
    if (nodeId < btComp.nodeTimers.size())
    {
        const float t = btComp.nodeTimers[nodeId];
        if (node.type == BTNodeType::Action && t >= 0.0f)
        {
            std::snprintf(buf, sizeof(buf), "  残り %.2fs", t);
            label += buf;
        }
        else if (node.type == BTNodeType::Decorator &&
                 node.decoratorType == BTDecoratorType::Cooldown)
        {
            std::snprintf(buf, sizeof(buf), "  CD残り %.2fs", (t > 0.0f) ? t : 0.0f);
            label += buf;
        }
    }
    if (nodeId < btComp.runningNodes.size() && !node.children.empty())
    {
        const int bookmark = btComp.runningNodes[nodeId];
        if (bookmark >= 0)
        {
            std::snprintf(buf, sizeof(buf), "  再開位置 %d/%d",
                          bookmark + 1, static_cast<int>(node.children.size()));
            label += buf;
        }
    }

    // スコア選択の子には、最後に選び直したときの点数を出す（なぜその技が選ばれたか・選ばれなかったか）
    //   点 … 点数、( % ) … 選ばれる確率、距離・状況・減点 … 点数に掛かった倍率、[選択] … 実際に選ばれた子
    //   倍率が 1（効いていない）のものは出さない
    if (parentIsUtility && nodeId < btComp.debugScores.size())
    {
        const BTScoreDebug &dbg = btComp.debugScores[nodeId];
        if (dbg.state != BTCandidateState::Candidate)
        {
            label += std::string("  候補外（") + ToString(dbg.state) + "）";
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "  点 %.1f (%.0f%%)", dbg.total, dbg.chance * 100.0f);
            label += buf;
            if (dbg.distance >= 0.0f)
            {
                std::snprintf(buf, sizeof(buf), "  距離 x%.2f", dbg.distance);
                label += buf;
            }
            if (dbg.bonus != 1.0f)
            {
                std::snprintf(buf, sizeof(buf), "  状況 x%.2f", dbg.bonus);
                label += buf;
            }
            if (dbg.repeat < 1.0f)
            {
                std::snprintf(buf, sizeof(buf), "  減点 x%.2f", dbg.repeat);
                label += buf;
            }
            if (dbg.picked)
            {
                label += "  [選択]";
            }
        }
    }

    ImGui::PushID(static_cast<int>(nodeId));

    // ---------------------------------------------------------
    // 3. 実行ボタン
    // ---------------------------------------------------------

    // もう一度押すと解除
    const bool isForced = (btComp.debugForcedNodeId == static_cast<int>(nodeId));
    if (ImGui::SmallButton(isForced ? "解除" : "実行"))
    {
        btComp.debugForcedNodeId = isForced ? -1 : static_cast<int>(nodeId);

        // 前の技の「今撃て」信号が残ると、止めたはずのドローンが飛び続ける。
        // 評価の処理にはボス固有の知識を持たせないので、この後始末はデバッグ UI だけで行う。
        if (auto *cmd = m_world->GetComponent<BossCommandComponent>(entity))
        {
            cmd->droneAttackActive = false;
        }
    }
    ImGui::SameLine();

    // ---------------------------------------------------------
    // 4. 行の描画
    // ---------------------------------------------------------

    // 今フレームの評価結果で色を変える（走っている枝が一目で分かる）
    ImVec4 color(0.75f, 0.75f, 0.75f, 1.0f);
    if (nodeId < btComp.debugNodeStates.size())
    {
        switch (btComp.debugNodeStates[nodeId])
        {
        case BTDebugState::Running:
            color = ImVec4(1.00f, 0.85f, 0.20f, 1.0f);
            break;
        case BTDebugState::Success:
            color = ImVec4(0.40f, 1.00f, 0.50f, 1.0f);
            break;
        case BTDebugState::Failure:
            color = ImVec4(0.90f, 0.45f, 0.40f, 1.0f);
            break;
        default:
            break;
        }
    }

    if (node.children.empty())
    {
        ImGui::TextColored(color, "#%d %s", static_cast<int>(nodeId), label.c_str());
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        const bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_DefaultOpen,
                                            "#%d %s", static_cast<int>(nodeId), label.c_str());
        ImGui::PopStyleColor();
        if (open)
        {
            for (BTNodeID child : node.children)
            {
                DrawNodeRowRecursive(asset, child, btComp, entity, node.type == BTNodeType::UtilitySelector);
            }
            ImGui::TreePop();
        }
    }

    ImGui::PopID();
}

void BehaviorTreeSystem::DrawForcedNodeGui(BehaviorTreeComponent &btComp, EntityID entity)
{
    if (!btComp.sharedAsset || btComp.sharedAsset->nodes.empty())
    {
        ImGui::TextDisabled("BT が読み込まれていません");
        return;
    }

    // 木の評価を止める。移動中に止めるとボスが歩き続けるので、行動の要求も合わせて消す。
    bool paused = btComp.debugPaused;
    if (ImGui::Checkbox("AIを止める（調整に集中）", &paused))
    {
        btComp.debugPaused = paused;
        if (paused)
        {
            if (auto *cmd = m_world->GetComponent<BossCommandComponent>(entity))
            {
                cmd->currentActionId = 0; // None → BossActionSystem は Idle
            }
        }
    }
    if (btComp.debugPaused)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           (btComp.debugForcedNodeId >= 0)
                               ? "強制実行中はこのチェックは効かない"
                               : "停止中（陣形指示は保持される）");
    }

    if (btComp.debugForcedNodeId >= 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "強制実行中: #%d", btComp.debugForcedNodeId);
        ImGui::SameLine();
        if (ImGui::SmallButton("通常のBTへ戻す"))
        {
            btComp.debugForcedNodeId = -1;
            if (auto *cmd = m_world->GetComponent<BossCommandComponent>(entity))
            {
                cmd->droneAttackActive = false;
            }
        }
    }
    else
    {
        ImGui::TextDisabled("通常評価（ルートから）");
    }

    ImGui::TextDisabled("「実行」を押した枝だけを繰り返し評価します。");
    ImGui::TextDisabled("強制中は間合いなどの条件をすべて通し、クールダウンも無視します。");
    ImGui::TextDisabled("技を丸ごと見るなら Sequence の行を、単発で見るなら Action の行を押す。");
    ImGui::Separator();

    DrawNodeRowRecursive(*btComp.sharedAsset, 0, btComp, entity);
}

void BehaviorTreeSystem::OnGui()
{
    auto view = m_world->View<BehaviorTreeComponent>();
    for (auto e : view)
    {
        auto *btComp = m_world->GetComponent<BehaviorTreeComponent>(e);
        if (!btComp) continue;

        ImGui::PushID(static_cast<int>(e));
        ImGui::Text("Entity ID: %llu", (unsigned long long)e);
        ImGui::Text("Asset Path: %s", btComp->assetPath.c_str());

        // lastLogActionId から行動名を逆引きする。行動が自分で書く値なので、A_Wait のように
        // 書かない行動の間は前の行動が残る（見出しを Last にしているのはそのため）。
        const ActionEntry *lastAction = FindActionEntry(btComp->lastLogActionId);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "Last Action: %s", lastAction ? lastAction->name : "Idle");

        ImGui::Text("Distance To Target: %.2fm", btComp->blackboard.distanceToTarget);

        // 角度と接近速度は符号に意味があるので、読み方を横に出す（右か左か、詰めか引きか）。
        ImGui::Text("Angle To Target  : %+.1f deg  [%s]", btComp->blackboard.angleToTarget,
                    (btComp->blackboard.angleToTarget >= 0.0f) ? "右" : "左");
        ImGui::Text("Closing Speed    : %+.2f m/s  [%s]", btComp->blackboard.targetClosingSpeed,
                    (btComp->blackboard.targetClosingSpeed >= 0.0f) ? "接近" : "後退");
        ImGui::Text("To Arena Edge    : %+.1f m", btComp->blackboard.distanceToArenaEdge);
        ImGui::Text("Target vs Center : %.1f deg  [0=中央側を塞がれている]", btComp->blackboard.targetAngleFromCenter);

        ImGui::Text("HP: %.1f%%", btComp->blackboard.healthPercentage);

        // 木を並べて「この技を今すぐ出す」を選べるようにする。
        if (ImGui::TreeNodeEx("強制実行（デバッグ）", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawForcedNodeGui(*btComp, e);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// 強制実行の適用（評価の並列ループから呼ばれる）
// ---------------------------------------------------------------------------

bool BehaviorTreeSystem::IgnoreGatesForDebug(const BehaviorTreeComponent &btComp)
{
    // 強制実行は「選んだ枝を今すぐ出す」ための機能。評価には、枝を出させないための関門がある：
    //   - Condition ノード（間合い・HP など）
    //   - Decorator の Cooldown
    //   - UtilitySelector のふるい（CandidatePasses）
    //   - Selector の割り込み・自己中断（走っている枝を打ち切って別の枝へ移る）
    // 強制実行中にこれらが効くと、その場の距離やクールダウン次第で「押したのに何も出ない」
    // 「出した直後に別の枝へ移る」になり、見たいものを見られない。なので全部素通しにする。
    //
    // 評価コードの中でデバッグのために分岐しているのは、この関数を呼んでいる所だけ。
    // 影響範囲を知りたいときは、この関数名で検索すれば全部見つかる。
    return btComp.debugForcedNodeId >= 0;
}

BTNodeID BehaviorTreeSystem::ApplyDebugForcing(BehaviorTreeComponent &btComp, size_t nodeCount)
{
    // 強制実行は、DrawForcedNodeGui で選んだ枝だけを評価させるデバッグ機能。
    // 選んだノードは debugForcedNodeId に入る（-1 は指定なし）。UI 側は値を書くだけで、
    // 実際に評価の開始点を差し替えるのはここ。木のデータは書き換えない。
    //
    // 個体ごとの btComp しか触らないので、並列ループから呼んでも競合しない。

    // 強制ノードが切り替わったら実行状態を捨てる。前の技の途中経過（再開位置・タイマー）が
    // 新しい枝へ持ち越されると、切り替え直後の1回だけ挙動がおかしくなる。
    // debugAppliedForcedNodeId は「最後に反映した指定」で、切り替わりの検出だけに使う。
    if (btComp.debugForcedNodeId != btComp.debugAppliedForcedNodeId)
    {
        btComp.debugAppliedForcedNodeId = btComp.debugForcedNodeId;
        std::fill(btComp.runningNodes.begin(), btComp.runningNodes.end(), -1);
        std::fill(btComp.nodeTimers.begin(), btComp.nodeTimers.end(), -1.0f);
    }

    // 指定が無いか、木の範囲外（木が差し替わって古い番号が残った場合など）ならルートから評価する
    if (btComp.debugForcedNodeId < 0 || btComp.debugForcedNodeId >= static_cast<int>(nodeCount))
    {
        return 0;
    }
    return static_cast<BTNodeID>(btComp.debugForcedNodeId);
}

void BehaviorTreeSystem::RecordUtilityPick(const BTAsset         &asset,
                                           BTNodeID               selectorId,
                                           BTNodeID               pickedId,
                                           const BTNodeID        *topIds,
                                           int                    topCount,
                                           BehaviorTreeComponent &btComp)
{
    // 処理フロー:
    // 1. 記録の枠を1つ取る
    // 2. 上位の内訳を写す（繰り上がりで選ばれた子がいれば足す）
    // 3. 上位が3つに足りなければ、空いた枠を候補外の子で埋める

    // 【備考】
    //   debugScores は次に選び直すと上書きされる。短い技（すぐ終わって次を選ぶ）だと
    //   1フレームで消えて画面では読めないので、選んだ瞬間の内訳をここで写して残す。
    //   個体ごとの btComp しか触らないので、並列ループから呼んでも競合しない。

    // 記録1件に上位3つ＋繰り上がりの1つが入ること
    static_assert(kPickRecordRows >= kUtilityTopCount + 1, "kPickRecordRows が上位の数より少ない");

    // ---------------------------------------------------------
    // 1. 枠を取る　いっぱいなら一番古いものを上書きする
    // ---------------------------------------------------------
    BTPickRecord &rec = btComp.debugPickLog[btComp.debugPickCount % kPickLogSize];
    ++btComp.debugPickCount;

    rec.selectorId = selectorId;
    rec.pickedId   = pickedId;
    rec.time       = btComp.debugClock;
    rec.distance   = btComp.blackboard.distanceToTarget;
    rec.rowCount   = 0;

    // ---------------------------------------------------------
    // 2. 上位
    // ---------------------------------------------------------
    bool pickedInTop = false;
    for (int i = 0; i < topCount && rec.rowCount < kPickRecordRows; ++i)
    {
        rec.rowIds[rec.rowCount] = topIds[i];
        rec.rows[rec.rowCount]   = btComp.debugScores[topIds[i]];
        ++rec.rowCount;
        if (topIds[i] == pickedId)
        {
            pickedInTop = true;
        }
    }

    // 上位の子が失敗し、繰り上がった子が選ばれたときは、その子も最後に足す
    if (!pickedInTop && rec.rowCount < kPickRecordRows)
    {
        rec.rowIds[rec.rowCount] = pickedId;
        rec.rows[rec.rowCount]   = btComp.debugScores[pickedId];
        ++rec.rowCount;
    }

    // ---------------------------------------------------------
    // 3. 候補外で空いた枠を埋める
    // ---------------------------------------------------------

    // 点数の高い順（＝候補に入っていれば選ばれやすかった順）に入れる。
    // 「出るはずの技がなぜ出ないか」を、表の空いた行で読めるようにするため
    const BTNode &selector = asset.nodes[selectorId];
    const int     limit    = (std::min)(static_cast<int>(selector.children.size()), kMaxWeightedSelectorChildren);
    bool          used[kMaxWeightedSelectorChildren] = {};
    while (rec.rowCount < kUtilityTopCount)
    {
        // まだ入れていない候補外の子のうち、点数が一番高いもの
        int best = -1;
        for (int i = 0; i < limit; ++i)
        {
            const BTScoreDebug &dbg      = btComp.debugScores[selector.children[i]];
            const bool          excluded = dbg.state != BTCandidateState::Candidate &&
                                           dbg.state != BTCandidateState::NotScored;
            if (!excluded || used[i])
            {
                continue;
            }
            if (best < 0 || dbg.total > btComp.debugScores[selector.children[best]].total)
            {
                best = i;
            }
        }
        if (best < 0)
        {
            break; // 候補外の子が残っていない
        }
        used[best] = true;

        const BTNodeID childId   = selector.children[best];
        rec.rowIds[rec.rowCount] = childId;
        rec.rows[rec.rowCount]   = btComp.debugScores[childId];
        if (rec.rows[rec.rowCount].state == BTCandidateState::Cooldown)
        {
            rec.rows[rec.rowCount].cooldownLeft = CooldownLeft(childId, btComp, asset);
        }
        ++rec.rowCount;
    }
}

float BehaviorTreeSystem::CooldownLeft(BTNodeID nodeId, const BehaviorTreeComponent &btComp, const BTAsset &asset)
{
    // TopCooldownActive と同じたどり方（重なった修飾を先頭の子へ降りる）で、一番長い残りを返す
    float left = 0.0f;
    for (BTNodeID id = nodeId; id < asset.nodes.size();)
    {
        const BTNode &node = asset.nodes[id];
        if (node.type != BTNodeType::Decorator)
        {
            break;
        }
        if (node.decoratorType == BTDecoratorType::Cooldown)
        {
            left = (std::max)(left, btComp.nodeTimers[id]);
        }
        if (node.children.empty())
        {
            break;
        }
        id = node.children[0];
    }
    return left;
}
