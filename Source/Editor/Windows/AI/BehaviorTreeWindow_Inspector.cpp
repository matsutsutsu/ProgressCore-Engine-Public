#include "Editor/Windows/AI/BehaviorTreeWindow.h"
#include "Editor/Windows/AI/BehaviorTreeEditorCommon.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace BTEditor;

void BehaviorTreeWindow::DrawInspector()
{
    // 編集の Undo は「入力を確定したとき」に1回だけ積む（ドラッグ中の毎フレームは積まない）。
    // 記録は関数の最後にまとめる。記録は m_nodes の中身を読み直すので、編集中のノードを指す
    // イテレータを使い終わってから行う。

    ZoneScopedN("BTW: Inspector");

    ImGui::Text("インスペクタ");
    ImGui::Separator();

    std::vector<ed::NodeId> selectedNodes;
    selectedNodes.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));

    if (nodeCount > 0)
    {
        uint32_t selectedId = ToEditorId(selectedNodes[0].Get());
        auto     it         = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const EditorBTNode &n) { return n.id == selectedId; });

        if (it != m_nodes.end())
        {
            // =========================================================
            // Undoを安全に記録するための遅延フラグ
            // =========================================================
            bool        needsUndo   = false;
            std::string undoMessage = "Inspector Changed";

            // ノード名。自分で付けるのはまとまり（Selector など）とコメント枠だけ。
            // Action / Condition / Decorator は下で選んだ内容から名前が決まるので、入力欄は出さず表示だけする。
            if (UsesCustomName(it->type))
            {
                char nameBuf[256];
                strcpy_s(nameBuf, it->name.c_str());
                if (ImGui::InputText("名前", nameBuf, sizeof(nameBuf)))
                {
                    it->name = nameBuf;
                }

                // 入力が完了（エンターかフォーカス外れ）した時だけUndo記録
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    needsUndo   = true;
                    undoMessage = "Change Node Name";
                }
            }
            else
            {
                ImGui::Text("名前: %s", GetDisplayName(*it).c_str());
                ImGui::TextDisabled("（設定から自動で決まる）");
            }

            // =========================================================
            // ノード種類ごとの説明
            // =========================================================
            ImGui::Spacing();

            // ウィンドウ幅で折り返す
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            switch (it->type)
            {
            case EditorBTNodeType::Selector:
                ImGui::TextUnformatted("子を左から順に実行し、成功した時点で成功を返す。\n"
                                       "優先したい行動ほど左に置く。");
                break;
            case EditorBTNodeType::Sequence:
                ImGui::TextUnformatted("子を左から順に実行し、失敗した時点で失敗を返す。\n"
                                       "条件 → 移動 → 攻撃 のような一連の流れに使う。");
                break;
            case EditorBTNodeType::Condition:
                ImGui::TextUnformatted("距離やHPを判定して成功/失敗を返す。\n"
                                       "Sequence の先頭に置く。");
                break;
            case EditorBTNodeType::Action:
                ImGui::TextUnformatted("移動や攻撃などの実処理。\n"
                                       "終わるまで時間がかかるものは Running を返し続ける。");
                break;
            case EditorBTNodeType::Decorator:
                ImGui::TextUnformatted("子を1つだけ持ち、その結果や実行タイミングを変える。\n"
                                       "反転 / クールダウン / リトライ");
                break;
            case EditorBTNodeType::Group:
                ImGui::TextUnformatted("ノードを囲んで整理するための枠。実行には影響しない。");
                break;
            case EditorBTNodeType::Parallel:
                ImGui::TextUnformatted("一番上の子が主。このノードの結果は主の結果になる。\n"
                                       "2つ目以降は従。主が Running の間だけ毎フレーム動き、結果は捨てる。\n"
                                       "従にボス本体の技（跳躍・後方ステップ・移動）は置かない。主が終わっても技が止まらなくなる。");
                break;
            case EditorBTNodeType::WeightedSelector:
                ImGui::TextUnformatted("子の重みに比例して1つ抽選し、その枝を実行する。\n"
                                       "失敗した枝は候補から外して引き直す。\n"
                                       "重みは子ノード側で設定（既定 1.0）。");
                break;
            case EditorBTNodeType::SubTree:
                ImGui::TextUnformatted("木を1本まとめた箱。中の入口に繋いだ木がこの位置で動く。\n"
                                       "保存時は中身に展開されるので、動作は直接置いた場合と同じ。\n"
                                       "ダブルクリックで中へ、パンくずで外へ。\n"
                                       "右クリック →「選択をサブツリーにまとめる」で作れる。");
                break;
            }
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::Separator();

            // ---------------------------------------------------------
            // 各種プロパティの編集とUndoフラグのセット
            // ---------------------------------------------------------
            if (it->type == EditorBTNodeType::Group)
            {
                ImGui::Text("枠の色");
                // 色見本を横に並べる。名前のコンボより、押した結果が想像しやすい。
                for (int i = 0; i < kGroupPaletteCount; ++i)
                {
                    if (i > 0) ImGui::SameLine();
                    ImGui::PushID(i);
                    const bool active = (it->groupColorIndex == i);
                    if (ImGui::ColorButton(kGroupPalette[i].name,
                                           ImGui::ColorConvertU32ToFloat4(kGroupPalette[i].header),
                                           ImGuiColorEditFlags_NoAlpha,
                                           ImVec2(active ? 28.0f : 22.0f, 22.0f)))
                    {
                        it->groupColorIndex = i;
                        needsUndo           = true;
                        undoMessage         = "Change Group Color";
                    }
                    ImGui::PopID();
                }
                ImGui::TextDisabled("名前が枠の見出しになる");
                ImGui::Separator();
            }

            if (it->type == EditorBTNodeType::Condition)
            {
                ImGui::Separator();
                ImGui::Text("条件");
                if (DrawConditionCombo("種類", it->actionOrConditionId))
                {
                    needsUndo             = true;
                    undoMessage           = "Change Condition";

                    // 引数を使う条件へ切り替えたのに 0 のままだと「距離0m未満」に
                    // なって永久に成立しない。最初の1回だけ既定値を入れておく。
                    if (GetConditionParamLabel(it->actionOrConditionId) && it->param <= 0.0f)
                        it->param = 10.0f;
                }

                // 引数を持つ条件（間合いなど）だけ入力欄を出す。行動側と同じ仕組み。
                if (const char *paramLabel = GetConditionParamLabel(it->actionOrConditionId))
                {
                    if (ImGui::DragFloat(paramLabel, &it->param, 0.5f, 0.0f, 500.0f, "%.1f"))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Condition Param";
                    }
                }
            }
            else if (it->type == EditorBTNodeType::Action)
            {
                ImGui::Separator();
                ImGui::Text("行動");
                if (DrawActionCombo("種類", it->actionOrConditionId))
                {
                    needsUndo             = true;
                    undoMessage           = "Change Action";

                    // 引数を使う行動へ切り替えたのに 0 のままだと「置いたのに何も起きない」
                    // ように見えるので、最初の1回だけ既定値を入れておく。
                    // データの技は枠番号なので既定値を入れない（1 は別の技の枠になる）。
                    if (it->actionOrConditionId != A_DroneFormation &&
                        GetActionParamLabel(it->actionOrConditionId) && it->param <= 0.0f)
                        it->param = 1.0f;
                }

                // データの技は数値ではなく技の名前で選ぶ。
                if (it->actionOrConditionId == A_DroneFormation)
                {
                    if (DrawDroneFormationCombo("技", it->param))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Drone Formation";
                    }
                }
                // 引数を持つ行動（待機の秒数、近接の硬直など）だけ入力欄を出す
                else if (const char *paramLabel = GetActionParamLabel(it->actionOrConditionId))
                {
                    if (ImGui::DragFloat(paramLabel, &it->param, 0.1f, 0.0f, 60.0f, "%.2f"))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Action Param";
                    }
                }

                // 選択中アクションの補足（最小構成 BossAction との対応）
                if (IsCommandWritingAction(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f),
                                       "BossActionSystem で実行（Parallel では主に置く）");
                }
                else if (IsDroneAction(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                                       "BossCommand.requestFormation へ陣形を渡す");
                }
                else if (IsWaitAction(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                                       "nodeTimers で待つ（BossAction は使わない）");
                }
                else if (it->actionOrConditionId == A_ResetDamage)
                {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                                       "Blackboard の蓄積ダメージを 0 に戻す");
                }
                else if (!FindActionEntry(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       "未登録の ID");
                }
            }

            if (it->type == EditorBTNodeType::SubTree)
            {
                ImGui::Separator();
                ImGui::Text("サブツリー");
                ImGui::TextDisabled("中のノード数: %d", static_cast<int>(CollectSubTreeContents(it->id).size()));
                if (ImGui::Button("中へ入る")) RequestEnterSubTree(it->id);
            }

            if (it->type == EditorBTNodeType::Decorator)
            {
                ImGui::Separator();
                ImGui::Text("デコレーター");

                // 子を待たずに並行して動かしたいときは Parallel ノードで書く。
                const char *decNames[] = {"反転 (Inverter)", "クールダウン (Cooldown)", "リトライ (Retry)"};
                if (ImGui::Combo("種類", &it->decoratorType, decNames, IM_ARRAYSIZE(decNames)))
                {
                    // 既定値を入れておく。0 のままだと Cooldown は毎フレーム素通り、
                    // Retry は子を一度も評価しない＝どちらも「置いたのに効かない」になる。
                    // 行動・条件の param が同じ理由で自動補完されるのに揃える。
                    if (it->param <= 0.0f)
                    {
                        if (it->decoratorType == 1)
                            it->param = 5.0f; // Cooldown [秒]
                        else if (it->decoratorType == 2)
                            it->param = 3.0f; // Retry [回]
                    }
                    needsUndo             = true;
                    undoMessage           = "Change Decorator Type";
                }

                if (it->decoratorType == 1)
                { // Cooldown
                    // ドラッグ中は数値を変えるだけ
                    ImGui::DragFloat("時間 [秒]", &it->param, 0.1f, 0.0f, 60.0f);

                    // マウスを離した時だけUndo記録
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        needsUndo   = true;
                        undoMessage = "Change Cooldown Time";
                    }
                }
                else if (it->decoratorType == 2)
                { // Retry
                    int retryCount = static_cast<int>(it->param);
                    if (ImGui::DragInt("回数", &retryCount, 1, 1, 10))
                    {
                        it->param = static_cast<float>(retryCount);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        needsUndo   = true;
                        undoMessage = "Change Retry Count";
                    }
                }
            }

            // 親が重み付きセレクターのときだけ、重みの欄を出す。
            //   重みは他の親の下では意味を持たないので、常時出すと
            //   「効かない設定」が並んで混乱する。
            {
                const EditorBTNode *parent = nullptr;
                for (const auto &lk : m_links)
                {
                    if (lk.endPinId != it->inputPin.id) continue;
                    for (const auto &n : m_nodes)
                    {
                        if (n.outputPin.id == lk.startPinId)
                        {
                            parent = &n;
                            break;
                        }
                    }
                    break;
                }

                // 割り込みは Selector の下でしか意味を持たない（上下関係があるのはそこだけ）。
                if (parent && parent->type == EditorBTNodeType::Selector)
                {
                    ImGui::Separator();
                    ImGui::Text("割り込み");

                    if (ImGui::Checkbox("下の枝の実行中も割り込む", &it->interrupt))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Interrupt";
                    }
                    ImGui::TextDisabled("下の枝の実行中もこの枝の条件を毎フレーム見て、");
                    ImGui::TextDisabled("成立したら中断して選び直す。");
                    ImGui::TextDisabled("枝の先頭に条件ノードが要る。");

                    ImGui::Separator();
                    ImGui::Text("自己中断");

                    if (ImGui::Checkbox("条件が外れたら中断する", &it->abortSelf))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Abort Self";
                    }
                    ImGui::TextDisabled("この枝の実行中も条件を毎フレーム見て、");
                    ImGui::TextDisabled("外れたら中断して選び直す。");
                    ImGui::TextDisabled("枝の先頭に条件ノードが要る。");
                }

                if (parent && parent->type == EditorBTNodeType::WeightedSelector)
                {
                    ImGui::Separator();
                    ImGui::Text("抽選");

                    ImGui::DragFloat("重み", &it->weight, 0.1f, 0.0f, 100.0f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        needsUndo             = true;
                        undoMessage           = "Change Weight";
                    }

                    // 数字だけでは何%になるか分からないので、兄弟の重みを合計して出す。
                    float total = 0.0f;
                    for (const auto &lk : m_links)
                    {
                        if (lk.startPinId != parent->outputPin.id) continue;
                        for (const auto &n : m_nodes)
                        {
                            if (n.inputPin.id == lk.endPinId && n.weight > 0.0f) total += n.weight;
                        }
                    }

                    if (it->weight <= 0.0f)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                                           "重みが 0 以下なので選ばれない");
                    }
                    else if (total > 0.0f)
                    {
                        ImGui::Text("確率 %.1f %%（合計 %.2f）",
                                    it->weight / total * 100.0f, total);
                        ImGui::TextDisabled("条件で外れる枝があれば、実際はこれより高い");
                    }
                }
            }

            // `it` を使い終わった一番最後に Undo を記録する。
            // 記録で m_nodes を読み直すと it が無効になりうるため
            if (needsUndo)
            {
                RecordStateForUndo(undoMessage);
            }
        }
    }
    else
    {
        ImGui::TextDisabled("ノードが選択されていない");
    }
}

bool BehaviorTreeWindow::DrawActionCombo(const char *label, ActionID &currentId)
{
    bool        changed = false;
    const char *preview = GetActionDisplayName(currentId);

    if (ImGui::BeginCombo(label, preview))
    {
        BTCategory lastCategory = BTCategory::None;
        for (int i = 0; i < g_ActionRegistryCount; ++i)
        {
            const ActionEntry &e = g_ActionRegistry[i];
            if (i == 0 || e.category != lastCategory)
            {
                ImGui::SeparatorText(BTCategoryName(e.category));
                lastCategory = e.category;
            }
            const bool selected = (e.id == currentId);
            if (ImGui::Selectable(e.name, selected))
            {
                currentId = e.id;
                changed   = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool BehaviorTreeWindow::DrawConditionCombo(const char *label, ActionID &currentId)
{
    bool        changed = false;
    const char *preview = GetConditionDisplayName(currentId);

    if (ImGui::BeginCombo(label, preview))
    {
        BTCategory lastCategory = BTCategory::None;
        for (int i = 0; i < g_ConditionRegistryCount; ++i)
        {
            const ActionEntry &e = g_ConditionRegistry[i];
            if (i == 0 || e.category != lastCategory)
            {
                ImGui::SeparatorText(BTCategoryName(e.category));
                lastCategory = e.category;
            }
            const bool selected = (e.id == currentId);
            if (ImGui::Selectable(e.name, selected))
            {
                currentId = e.id;
                changed   = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool BehaviorTreeWindow::DrawDroneFormationCombo(const char *label, float &slotParam)
{
    bool      changed = false;
    const int current = static_cast<int>(std::lround(slotParam));

    if (ImGui::BeginCombo(label, GetDroneFormationLabel(slotParam)))
    {
        for (size_t slot = 0; m_formations && slot < kDroneFormationSlotCount; ++slot)
        {
            const char *name = m_formations->DataFormationName(static_cast<DroneFormationType>(slot));
            if (!name) continue; // C++ 実装の枠と空き枠は出さない（ここで選べるのはデータの技だけ）

            char item[96];
            snprintf(item, sizeof(item), "%s  (枠%zu)", name, slot);

            const bool selected = (static_cast<int>(slot) == current);
            if (ImGui::Selectable(item, selected))
            {
                slotParam = static_cast<float>(slot);
                changed   = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("陣形エディタで作った技。BT には枠番号で保存されるので、名前は後から変えてよい");

    return changed;
}
