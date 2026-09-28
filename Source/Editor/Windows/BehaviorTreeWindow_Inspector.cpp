// BehaviorTreeWindow：右ペイン（選択中のノードの編集欄）。
//
// 編集の Undo は「入力を確定したとき」に1回だけ積む（ドラッグ中の毎フレームは積まない）。
// 記録は関数の最後にまとめる。記録は m_nodes の中身を読み直すので、編集中のノードを指す
// イテレータを使い終わってから行う。
#include "BehaviorTreeWindow.h"
#include "BehaviorTreeEditorCommon.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace BTEditor;

void BehaviorTreeWindow::DrawInspector()
{
    ImGui::Text("インスペクタ (Inspector)");
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
                if (ImGui::InputText("名前 (Name)", nameBuf, sizeof(nameBuf)))
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
                ImGui::TextDisabled("（下の設定から自動で決まります）");
            }

            // =========================================================
            // ノード種類ごとの説明
            // =========================================================
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f)); // 黄色テキスト
            ImGui::Text("[ノードの役割と使い方]");
            ImGui::PopStyleColor();

            // 自動折り返しを有効にして、ウィンドウ幅に合わせてテキストを改行させる
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);

            switch (it->type)
            {
            case EditorBTNodeType::Selector:
                ImGui::Text("【Selector (セレクター / OR条件)】\n"
                            "子ノードを左から順に実行し、どれか1つでも「Success(成功)」すれば親にSuccessを返して終了します。\n"
                            "💡 使い方：優先度の高い行動（左）から順番に試す「行動の分岐点」として使います。");
                break;
            case EditorBTNodeType::Sequence:
                ImGui::Text("【Sequence (シーケンス / AND条件)】\n"
                            "子ノードを左から順に実行し、1つでも「Failure(失敗)」したら親にFailureを返して終了します。\n"
                            "💡 使い方：「条件チェック」→「近づく」→「攻撃する」といった、一連のアクションを順番にこなすセットを作るのに使います。");
                break;
            case EditorBTNodeType::Condition:
                ImGui::Text("【Condition (条件)】\n"
                            "状況（距離やHPなど）を判定し、SuccessかFailureを即座に返します。\n"
                            "💡 使い方：Sequenceの先頭（一番左）に置いて、その行動セットを実行してよいかの「門番」として使います。");
                break;
            case EditorBTNodeType::Action:
                ImGui::Text("【Action (アクション / 行動)】\n"
                            "実際にAIを動かす末端の命令です。ゲームのロジックに命令を送ります。\n"
                            "💡 使い方：移動や攻撃など。実行に時間がかかる行動は「Running(実行中)」を返し続け、次フレームも呼ばれます。\n"
                            "📌 現状のBoss実行層が対応するのは「移動(Move)」のみ。ドローン系は陣形指示、Waitはタイマー待機です。");
                break;
            case EditorBTNodeType::Decorator:
                ImGui::Text("【Decorator (デコレーター / 修飾)】\n"
                            "下に1つだけ子ノードを持ち、その子の結果や実行タイミングを操作します。\n"
                            "💡 使い方：強力な攻撃に「10秒のクールダウン」をつけたり、失敗時に「2回リトライ」させたりする制限をかけるのに使います。");
                break;
            case EditorBTNodeType::Group:
                ImGui::Text("【Group (グループ / コメント)】\n"
                            "複数のノードを視覚的に囲んで整理するための枠です。\n"
                            "💡 使い方：AIの実行ロジックには一切影響を与えません。メモや整理に使ってください。");
                break;
            case EditorBTNodeType::Parallel:
                ImGui::Text("【Parallel (並行ノード)】\n"
                            "一番上につないだ子が「主」で、その結果がこのノードの結果になります。\n"
                            "2つ目以降は「従」で、主が実行中の間だけ毎フレーム動き、結果は無視されます。\n"
                            "💡 使い方：技を出しながら、その間ドローンにも指示を出す、といった同時進行に使います。\n"
                            "⚠ 従にはボス本体の技（跳躍・後方ステップ・移動）を置かないでください。"
                            "主が終わると従は止まりますが、体への要求が下りず技が続いてしまいます。");
                break;
            case EditorBTNodeType::WeightedSelector:
                ImGui::Text("【Weighted Selector (重み付きセレクター)】\n"
                            "子ノードの「重み」に比例して1つ抽選し、その枝を実行します。\n"
                            "失敗したらその枝を候補から外して、残りから引き直します。\n"
                            "重みを全部同じにすれば、ただの均等ランダムになります（既定は 1.0）。\n"
                            "💡 使い方：敵の近接攻撃の3択など、プレイヤーに次の行動を予測させない「揺さぶり（Mix-up）」に最適です。\n"
                            "💡 重みは子ノードを選んで設定します。実際の確率もそこに出ます。");
                break;
            case EditorBTNodeType::SubTree:
                ImGui::Text("【SubTree (サブツリー)】\n"
                            "中に木を1本しまっておく箱です。実行すると、中の「入口」に繋いだ木がこの場所で動きます。\n"
                            "保存時に中身へ置き換わるので、ゲームの動きは中身を直接置いた場合と同じです。\n"
                            "💡 使い方：ひとまとまりの行動（近接の3択など）を箱に入れて、外側の木を短く保ちます。"
                            "既存のノードは、選んで右クリック →「選択をサブツリーにまとめる」で箱に入れられます。\n"
                            "💡 ダブルクリックで中へ入り、ツールバーの下のパンくずで外へ戻れます。");
                break;
            }
            ImGui::PopTextWrapPos(); // 自動折り返し終了
            ImGui::Separator();

            // ---------------------------------------------------------
            // 各種プロパティの編集とUndoフラグのセット
            // ---------------------------------------------------------
            if (it->type == EditorBTNodeType::Group)
            {
                ImGui::Text("枠の色:");
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
                ImGui::TextDisabled("上の「名前 (Name)」がそのまま枠の見出しになります");
                ImGui::Separator();
            }

            if (it->type == EditorBTNodeType::Condition)
            {
                ImGui::Separator();
                ImGui::Text("条件設定 (Condition):");
                if (DrawConditionCombo("条件を選ぶ", it->actionOrConditionId))
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
                ImGui::Text("行動設定 (Action):");
                if (DrawActionCombo("行動を選ぶ", it->actionOrConditionId))
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
                    if (DrawDroneFormationCombo("技を選ぶ", it->param))
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
                                       "→ BossActionSystem が体を動かす（Parallel では主に置く）");
                }
                else if (IsDroneAction(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                                       "→ BossCommand.requestFormation に陣形を指示");
                }
                else if (IsWaitAction(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                                       "→ nodeTimers で待機（BossAction 非依存）");
                }
                else if (it->actionOrConditionId == A_ResetDamage)
                {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                                       "→ ブラックボードの蓄積ダメージをリセット");
                }
                else if (!FindActionEntry(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       "⚠ 未登録ID（辞書に載っていない番号）");
                }
            }

            if (it->type == EditorBTNodeType::SubTree)
            {
                ImGui::Separator();
                ImGui::Text("サブツリー (SubTree):");
                ImGui::TextDisabled("中のノード数: %d", static_cast<int>(CollectSubTreeContents(it->id).size()));
                if (ImGui::Button("中へ入る")) RequestEnterSubTree(it->id);
            }

            if (it->type == EditorBTNodeType::Decorator)
            {
                ImGui::Separator();
                ImGui::Text("デコレーター設定 (Decorator):");

                // 旧「3: 非同期 (Async)」は削除した。同じ用途は Parallel ノードで書く。
                const char *decNames[] = {"0: 結果反転 (Inverter)", "1: クールダウン (Cooldown)", "2: リトライ (Retry)"};
                if (ImGui::Combo("タイプ", &it->decoratorType, decNames, IM_ARRAYSIZE(decNames)))
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
                    ImGui::DragFloat("待機時間 (秒)", &it->param, 0.1f, 0.0f, 60.0f);

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
                    if (ImGui::DragInt("リトライ回数", &retryCount, 1, 1, 10))
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

                if (parent && parent->type == EditorBTNodeType::WeightedSelector)
                {
                    ImGui::Separator();
                    ImGui::Text("抽選の重み (Weight):");

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
                                           "重みが0以下なので、この枝は一度も選ばれません");
                    }
                    else if (total > 0.0f)
                    {
                        ImGui::Text("選ばれる確率  %.1f %%   (兄弟の重みの合計 %.2f)",
                                    it->weight / total * 100.0f, total);
                        ImGui::TextDisabled("条件で弾かれた枝は候補から外れるので、実際はこれより上がります");
                    }
                }
            }

            // =========================================================
            // すべての `it` の使用が終わった「一番最後」にUndoを記録する！
            // これによりイテレータ無効化によるクラッシュを完全に防ぐ
            // =========================================================
            if (needsUndo)
            {
                RecordStateForUndo(undoMessage);
            }
        }
    }
    else
    {
        ImGui::TextDisabled("編集するノードを選択してください。");
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
        auto &registry = CCL::AI::Formation::FormationRegistry::Instance();
        for (size_t slot = 0; slot < kDroneFormationSlotCount; ++slot)
        {
            const char *name = registry.DataFormationName(static_cast<DroneFormationType>(slot));
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
        ImGui::SetTooltip("陣形エディタで作った技が並ぶ。BTが覚えるのは枠番号なので、技の名前は後から変えてよい。");

    return changed;
}
