#include "Editor/Windows/AI/BehaviorTreeWindow.h"
#include "Editor/Windows/AI/BehaviorTreeEditorCommon.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Game/Logics/AI/BehaviorTree/BTUtilityScore.h" // プレビューの点数をゲームと同じ式で出す
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace BTEditor;

void BehaviorTreeWindow::DrawInspector()
{
    // 処理フロー:
    // 1. 名前
    // 2. 種類ごとの説明
    // 3. 種類ごとの設定（コメント枠・条件・行動・デコレーター・スコア選択の一覧）
    // 4. 親の種類で意味が決まる設定（割り込み・重み・点数）
    // 5. Undo の記録

    // 【備考】
    //   編集の Undo は「入力を確定したとき」に1回だけ積む（ドラッグ中の毎フレームは積まない）。
    //   記録は 5 にまとめる。記録は m_nodes の中身を読み直すので、編集中のノードを指す
    //   イテレータを使い終わってから行う。

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
            bool        needsUndo   = false;               // 5 で Undo を記録するか
            std::string undoMessage = "Inspector Changed"; // 履歴に出す操作名

            // ---------------------------------------------------------
            // 1. 名前
            // ---------------------------------------------------------

            // 自分で付けるのはまとまり（Selector など）とコメント枠だけ。
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

            // ---------------------------------------------------------
            // 2. 種類ごとの説明
            // ---------------------------------------------------------
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
            case EditorBTNodeType::UtilitySelector:
                ImGui::TextUnformatted("出せる子のうち、点数の高い上位3つから抽選する。\n"
                                       "点数は子ノード側で設定。");
                break;
            }
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::Separator();

            // ---------------------------------------------------------
            // 3. 種類ごとの設定
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
                    // 技を指す行動は枠番号なので既定値を入れない（1 は別の技の枠になる）。
                    if (it->actionOrConditionId != A_DroneFormation &&
                        it->actionOrConditionId != A_DroneTechnique &&
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
                else if (it->actionOrConditionId == A_DroneTechnique)
                {
                    if (DrawDroneTechniqueCombo("技", it->param))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Drone Technique";
                    }
                    DrawDroneTechniqueTiming(it->param);
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

            // スコア選択は、子の設定を1つずつ開かずに並べて比べられるよう、ここに一覧を出す
            if (it->type == EditorBTNodeType::UtilitySelector)
            {
                DrawUtilityOverview(*it, needsUndo, undoMessage);
            }

            // ---------------------------------------------------------
            // 4. 親の種類で意味が決まる設定
            // ---------------------------------------------------------

            // 割り込み・重み・点数は、親の種類が合うときだけ出す。
            // 他の親の下では意味を持たないので、常時出すと「効かない設定」が並んで混乱する。
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

                // 割り込みと自己中断は Selector とスコア選択の下で効く。
                // 説明は仕組みではなく「どんな場面で使うか」を書く（仕組みはコードのコメントにある）
                const bool canAbort = parent && (parent->type == EditorBTNodeType::Selector ||
                                                 parent->type == EditorBTNodeType::UtilitySelector);
                if (canAbort)
                {
                    ImGui::Separator();
                    ImGui::Text("割り込み");

                    // スコア選択の子には上下が無いので、相手は「下の枝」ではなく「ほかの技」
                    const char *label = (parent->type == EditorBTNodeType::Selector) ? "下の枝の実行中も割り込む"
                                                                                     : "ほかの技の実行中も割り込む";
                    if (ImGui::Checkbox(label, &it->interrupt))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Interrupt";
                    }
                    ImGui::TextDisabled("何をしていても、すぐに出したい技に使う。");
                    ImGui::TextDisabled("例：被弾がかさんだら退く／体幹が崩れたら怯む");
                    ImGui::TextDisabled("枝の先頭に条件を置いて使う。");

                    ImGui::Separator();
                    ImGui::Text("自己中断");

                    if (ImGui::Checkbox("条件が外れたら中断する", &it->abortSelf))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Abort Self";
                    }
                    ImGui::TextDisabled("前提が崩れたら、途中でやめたい技に使う。");
                    ImGui::TextDisabled("例：弾幕の途中で近づかれたらやめる");
                    ImGui::TextDisabled("枝の先頭に条件を置いて使う。");
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

                // 確率は「その場で出せる子」と「上位3つに入るか」で決まるので、エディタでは出さない
                if (parent && parent->type == EditorBTNodeType::UtilitySelector)
                {
                    ImGui::Separator();
                    ImGui::Text("スコア");

                    if (ImGui::Checkbox("距離で点数を変える", &it->useScoreRule))
                    {
                        // 入れた瞬間に出やすさが変わらないよう、今の点数（weight）を優先度に引き継ぐ。
                        // 優先度を一度でも触っていれば（既定の 1 でなければ）そちらを残す
                        if (it->useScoreRule && it->scoreRule.base == 1.0f)
                        {
                            it->scoreRule.base = it->weight;
                        }
                        needsUndo   = true;
                        undoMessage = "Toggle Score Rule";
                    }

                    if (!it->useScoreRule)
                    {
                        // ルールなし：weight をそのまま点数にする（保存先も weight）
                        ImGui::DragFloat("点数", &it->weight, 0.1f, 0.0f, 100.0f, "%.2f");
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            needsUndo   = true;
                            undoMessage = "Change Score";
                        }

                        if (it->weight <= 0.0f)
                        {
                            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "0 以下は選ばれない");
                        }
                    }
                    else
                    {
                        // ルールあり：点数 = 優先度 × 距離の台形（0〜1）
                        BTScoreRule &r = it->scoreRule;

                        ImGui::DragFloat("優先度", &r.base, 0.1f, 0.0f, 100.0f, "%.2f");
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            needsUndo   = true;
                            undoMessage = "Change Score Base";
                        }

                        // 得意な距離は最小と最大を1行で入れる（DragFloatRange2 は最小 > 最大にならないよう抑える）
                        ImGui::DragFloatRange2("得意な距離", &r.bestMin, &r.bestMax, 0.1f, 0.0f, 9999.0f,
                                               "%.1f m", "%.1f m");
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            needsUndo   = true;
                            undoMessage = "Change Score Distance";
                        }

                        ImGui::DragFloat("ぼかし幅", &r.falloff, 0.1f, 0.0f, 9999.0f, "%.1f m");
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            needsUndo   = true;
                            undoMessage = "Change Score Falloff";
                        }

                        // 読み込み時にも警告するが、ここで出せば入れ違えたその場で気づける（JSON を手で直した場合など）
                        if (r.bestMin > r.bestMax || r.falloff < 0.0f)
                        {
                            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "距離の値が不正");
                        }
                        if (r.base <= 0.0f)
                        {
                            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "0 以下は選ばれない");
                        }
                        ImGui::TextDisabled("範囲外はぼかし幅で 0 点まで下がる");

                        // クールダウン：選んだ瞬間から数える（技の最中も減る）
                        ImGui::Spacing();
                        ImGui::DragFloat("クールダウン", &r.cooldown, 0.1f, 0.0f, 120.0f, "%.1f 秒");
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            needsUndo   = true;
                            undoMessage = "Change Score Cooldown";
                        }
                        ImGui::TextDisabled("連発させたくない技に使う。例：突進は10秒に1回まで");

                        // 状況による倍率：条件が通れば点数に倍率を掛ける。消すと後ろを前へ詰める
                        ImGui::Spacing();
                        ImGui::Text("状況による倍率");
                        for (int i = 0; i < r.bonusCount; ++i)
                        {
                            BTScoreBonus &b = r.bonuses[i];
                            ImGui::PushID(i);

                            if (DrawConditionCombo("条件", b.conditionId))
                            {
                                needsUndo   = true;
                                undoMessage = "Change Score Bonus";
                            }
                            if (const char *paramLabel = GetConditionParamLabel(b.conditionId))
                            {
                                ImGui::DragFloat(paramLabel, &b.param, 0.1f, 0.0f, 500.0f, "%.1f");
                                if (ImGui::IsItemDeactivatedAfterEdit())
                                {
                                    needsUndo   = true;
                                    undoMessage = "Change Score Bonus";
                                }
                            }
                            ImGui::DragFloat("倍率", &b.mult, 0.05f, 0.0f, 10.0f, "x%.2f");
                            if (ImGui::IsItemDeactivatedAfterEdit())
                            {
                                needsUndo   = true;
                                undoMessage = "Change Score Bonus";
                            }

                            const bool remove = ImGui::SmallButton("削除");
                            ImGui::PopID();
                            ImGui::Separator();

                            if (remove)
                            {
                                for (int k = i; k + 1 < r.bonusCount; ++k)
                                {
                                    r.bonuses[k] = r.bonuses[k + 1];
                                }
                                --r.bonusCount;
                                needsUndo   = true;
                                undoMessage = "Remove Score Bonus";
                                break; // 並びが変わったので、この先は次のフレームに描く
                            }
                        }

                        if (r.bonusCount < kMaxScoreBonuses && ImGui::Button("倍率を追加"))
                        {
                            r.bonuses[r.bonusCount++] = BTScoreBonus{0, 0.0f, 1.5f};
                            needsUndo                 = true;
                            undoMessage               = "Add Score Bonus";
                        }
                    }
                }
            }

            // ---------------------------------------------------------
            // 5. Undo の記録
            // ---------------------------------------------------------

            // `it` を使い終わった一番最後に記録する。記録で m_nodes を読み直すと it が無効になりうるため
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

bool BehaviorTreeWindow::DrawDroneTechniqueCombo(const char *label, float &slotParam)
{
    bool      changed = false;
    const int current = static_cast<int>(std::lround(slotParam));

    if (ImGui::BeginCombo(label, GetDroneTechniqueLabel(slotParam)))
    {
        for (int slot = 0; slot < static_cast<int>(kDroneFormationSlotCount); ++slot)
        {
            const char *name = DroneTechniqueName(slot);
            if (!name) continue; // 円陣などの隊形と空き枠は出さない

            char item[96];
            snprintf(item, sizeof(item), "%s  (枠%d)", name, slot);

            const bool selected = (slot == current);
            if (ImGui::Selectable(item, selected))
            {
                slotParam = static_cast<float>(slot);
                changed   = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void BehaviorTreeWindow::DrawDroneTechniqueTiming(float slotParam) const
{
    const int slot = static_cast<int>(std::lround(slotParam));
    if (!DroneTechniqueName(slot)) return;

    const CCL::AI::Formation::IFormation *formation = m_formations->Get(static_cast<DroneFormationType>(slot));
    CCL::AI::Formation::TechniqueTiming   timing;
    formation->TryGetTiming(timing);

    ImGui::Text("予告 %.2f 秒 → 撃って %.2f 秒で戻す → 間 %.2f 秒",
                timing.telegraph, timing.recovery, timing.rest);

    // 時間は BT ではなく陣形が持つ
    if (formation->DisplayName())
        ImGui::TextDisabled("時間は陣形エディタで変える");
    else
        ImGui::TextDisabled("時間は C++ の陣形クラスで決めている");
}

// ---------------------------------------------------------------------------
// スコア選択の一覧
// ---------------------------------------------------------------------------

namespace
{
    // プレビューの距離のスライダーの右端 [m]。ボスの技の間合い（遠距離は 80m 超）が収まる長さ
    constexpr float kPreviewMaxDistance = 120.0f;

    // プレビュー用の点数。ゲームと同じ「優先度 × 距離の倍率」だが、条件・クールダウン・
    // 状況による倍率・繰り返しの減点はゲームの状況で決まるので含めない
    float PreviewScore(const EditorBTNode &child, float distance)
    {
        if (!child.useScoreRule)
        {
            return child.weight;
        }
        return child.scoreRule.base * CCL::AI::BT::DistanceFactor(child.scoreRule, distance);
    }
} // namespace

std::string BehaviorTreeWindow::GetUtilityChildName(const EditorBTNode &child)
{
    // 技の枝は「Cooldown → Sequence（名前付き）」の形が多い。Cooldown の表示名（"Cooldown 7秒"）では
    // どの技か分からないので、子を1つだけ持つ修飾を下へたどり、名前の付いたまとまりを探す
    const EditorBTNode *node = &child;
    while (node && node->type == EditorBTNodeType::Decorator)
    {
        auto found = m_childrenOf.find(node->id);
        if (found == m_childrenOf.end() || found->second.empty())
        {
            break;
        }
        node = FindNode(found->second.front());
    }

    if (node && UsesCustomName(node->type) && !node->name.empty())
    {
        return node->name;
    }
    return GetDisplayName(child);
}

void BehaviorTreeWindow::DrawUtilityOverview(const EditorBTNode &selector, bool &needsUndo, std::string &undoMessage)
{
    // 処理フロー:
    // 1. 子を集め、プレビューの距離での点数と確率を出す
    // 2. プレビューの距離のスライダー
    // 3. 子の一覧表（優先度・距離はその場で編集できる）

    // 【備考】
    //   子を1つずつ選んで設定を見比べるのは手間なので、全員をここに並べる。
    //   プレビューは距離と優先度だけで計算する。実行中の本当の点数は BehaviorTreeSystem の OnGui で見る。

    ImGui::Separator();
    ImGui::Text("候補の一覧");

    auto found = m_childrenOf.find(selector.id);
    if (found == m_childrenOf.end() || found->second.empty())
    {
        ImGui::TextDisabled("子がない");
        return;
    }
    const std::vector<uint32_t> &children = found->second;

    // ---------------------------------------------------------
    // 1. 点数と確率
    // ---------------------------------------------------------
    struct Row
    {
        EditorBTNode *node;   // 子
        std::string   name;   // 表に出す名前
        float         score;  // プレビューの距離での点数
        float         chance; // 選ばれる確率（上位3つの外は 0）
    };

    std::vector<Row> rows;
    for (uint32_t childId : children)
    {
        EditorBTNode *child = FindNode(childId);
        if (!child)
        {
            continue;
        }
        rows.push_back({child, GetUtilityChildName(*child), PreviewScore(*child, m_utilityPreviewDistance), 0.0f});
    }

    // ゲームと同じく、点数の高い上位から比例して引く（同点は元の並び順。stable_sort なので保たれる）
    std::vector<size_t> order;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].score > 0.0f)
        {
            order.push_back(i);
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return rows[a].score > rows[b].score; });

    const size_t top   = (std::min)(order.size(), static_cast<size_t>(CCL::AI::BT::kUtilityTopCount));
    float        total = 0.0f;
    for (size_t i = 0; i < top; ++i)
    {
        total += rows[order[i]].score;
    }
    for (size_t i = 0; i < top; ++i)
    {
        rows[order[i]].chance = rows[order[i]].score / total;
    }

    // ---------------------------------------------------------
    // 2. プレビューの距離
    // ---------------------------------------------------------
    ImGui::SliderFloat("プレビューの距離", &m_utilityPreviewDistance, 0.0f, kPreviewMaxDistance, "%.0f m");
    ImGui::TextDisabled("距離と優先度だけで計算（条件・クールダウン・状況による倍率・減点は含めない）");

    // ---------------------------------------------------------
    // 3. 一覧表
    // ---------------------------------------------------------

    // 一番見たいのは「どれが選ばれやすいか」なので、技の名前のすぐ右に確率と点数を置く。
    // 横にはみ出したら横スクロール。縦は行の数ぶんの高さを取る（スクロールさせると一覧の意味が薄れる）
    const float           tableHeight = ImGui::GetFrameHeightWithSpacing() * static_cast<float>(rows.size() + 1) +
                                        ImGui::GetStyle().ScrollbarSize + 4.0f;
    const ImGuiTableFlags flags       = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
    if (!ImGui::BeginTable("##utilityTable", 8, flags, ImVec2(0.0f, tableHeight)))
    {
        return;
    }

    ImGui::TableSetupColumn("技");
    ImGui::TableSetupColumn("確率");
    ImGui::TableSetupColumn("点数");
    ImGui::TableSetupColumn("優先度");
    ImGui::TableSetupColumn("得意な距離");
    ImGui::TableSetupColumn("ぼかし");
    ImGui::TableSetupColumn("状況");
    ImGui::TableSetupColumn("CD");
    ImGui::TableHeadersRow();

    for (Row &row : rows)
    {
        EditorBTNode &child = *row.node;
        ImGui::PushID(static_cast<int>(child.id));
        ImGui::TableNextRow();

        // 技：名前を押すとその子を選ぶ（状況による倍率はその子のインスペクタで編集する）
        ImGui::TableNextColumn();
        if (ImGui::Selectable(row.name.c_str()))
        {
            ed::ClearSelection();
            ed::SelectNode(child.id);
            ed::NavigateToSelection(false, 0.3f);
        }

        // 確率と点数（プレビューの距離での値）
        ImGui::TableNextColumn();
        if (row.chance > 0.0f)
        {
            ImGui::Text("%.0f%%", row.chance * 100.0f);
        }
        else
        {
            ImGui::TextDisabled("-");
        }

        ImGui::TableNextColumn();
        if (row.score > 0.0f)
        {
            ImGui::Text("%.1f", row.score);
        }
        else
        {
            ImGui::TextDisabled("候補外");
        }

        // 優先度：距離で変えない子は weight がそのまま優先度（保存先が違うだけで意味は同じ）
        ImGui::TableNextColumn();
        float &priority = child.useScoreRule ? child.scoreRule.base : child.weight;
        ImGui::SetNextItemWidth(60.0f);
        ImGui::DragFloat("##priority", &priority, 0.1f, 0.0f, 100.0f, "%.1f");
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            needsUndo   = true;
            undoMessage = "Change Score";
        }

        // 得意な距離：距離で変えない子にはボタンだけ出す。押すと距離で変える設定に切り替える
        ImGui::TableNextColumn();
        if (child.useScoreRule)
        {
            ImGui::SetNextItemWidth(130.0f);
            ImGui::DragFloatRange2("##range", &child.scoreRule.bestMin, &child.scoreRule.bestMax, 0.1f, 0.0f, 9999.0f,
                                   "%.0f", "%.0f m");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                needsUndo   = true;
                undoMessage = "Change Score Distance";
            }
        }
        else if (ImGui::SmallButton("距離で変える"))
        {
            child.useScoreRule   = true;
            child.scoreRule.base = child.weight; // 出やすさを変えずに切り替える
            needsUndo            = true;
            undoMessage          = "Toggle Score Rule";
        }

        // ぼかし
        ImGui::TableNextColumn();
        if (child.useScoreRule)
        {
            ImGui::SetNextItemWidth(60.0f);
            ImGui::DragFloat("##falloff", &child.scoreRule.falloff, 0.1f, 0.0f, 9999.0f, "%.0f m");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                needsUndo   = true;
                undoMessage = "Change Score Falloff";
            }
        }
        else
        {
            ImGui::TextDisabled("-");
        }

        // 状況：件数だけ出し、中身はマウスを乗せたときに出す（表の幅を取らないため）
        ImGui::TableNextColumn();
        if (child.useScoreRule && child.scoreRule.bonusCount > 0)
        {
            ImGui::Text("%d件", static_cast<int>(child.scoreRule.bonusCount));
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                for (int b = 0; b < child.scoreRule.bonusCount; ++b)
                {
                    const BTScoreBonus &bonus = child.scoreRule.bonuses[b];
                    const char         *name  = GetConditionDisplayName(bonus.conditionId);
                    ImGui::Text("%s（%g） x%.2f", name ? name : "未設定", bonus.param, bonus.mult);
                }
                ImGui::EndTooltip();
            }
        }
        else
        {
            ImGui::TextDisabled("-");
        }

        // クールダウン（採点ルールの cooldown）。距離で変えない子はルールが無いので持てない
        ImGui::TableNextColumn();
        if (child.useScoreRule)
        {
            ImGui::SetNextItemWidth(60.0f);
            ImGui::DragFloat("##cooldown", &child.scoreRule.cooldown, 0.1f, 0.0f, 120.0f, "%.0f 秒");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                needsUndo   = true;
                undoMessage = "Change Score Cooldown";
            }
        }
        else
        {
            ImGui::TextDisabled("-");
        }

        ImGui::PopID();
    }
    ImGui::EndTable();
}
