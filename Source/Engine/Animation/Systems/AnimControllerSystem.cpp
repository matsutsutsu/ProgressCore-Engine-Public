#include "AnimControllerSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <imgui.h>
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Animation/Core/AnimGraphSerializer.h"
#include "Engine/Platform/Core/Logger.h"
#include <tracy/Tracy.hpp>

// ===========================================================================
// 超速化: ビットマスクによるキャンセルウィンドウの評価
// ===========================================================================
static bool IsInCancelWindow(const AnimatorComponent &animator, uint32_t requiredMask)
{
    if (requiredMask == 0)
        return true; // マスク指定なし(0)なら常時許可

    const AnimSequence *seq = animator.currentSequence;
    if (!seq)
        return false;

    for (const auto &ev : seq->events)
    {
        if (ev.type != AnimEventType::ComboWindow &&
            ev.type != AnimEventType::CancelWindow)
            continue;

        // DODの極み: 窓のマスクと、遷移が要求するマスクのビットが「1つでも」被っていればOK
        if ((ev.cancelMask & requiredMask) == 0)
            continue;

        if (animator.currentTimer >= ev.startTime &&
            animator.currentTimer <= ev.endTime)
            return true;
    }
    return false;
}

// ===========================================================================
// 階層ステートマシン（サブステートマシン）評価の補助関数群
//   設計：現在地は fsm.activePath [root側..., 葉]。評価は「外→内」に降りる。
//   各階層で ① このグラフの anyState（外側の割り込み）→ ② 現在ステートがサブマシンなら
//   その out 遷移（＝サブマシン全体の出口）→ ③ 子グラフへ降下、の順に見る。
//   これで「外側（親）の割り込みが内側（子）より優先」という階層FSMの意味論になる。
// ===========================================================================
namespace
{
    // このグラフの直下ステートを hash で引く（子グラフは辿らない）。
    const AnimState *FindDirectState(const AnimStateGraph &g, uint32_t hash)
    {
        for (const auto &s : g.states)
            if (s.stateHash == hash) return &s;
        return nullptr;
    }

    // graph 内の target へ「入り」、サブマシンなら entry へ潜って葉まで path を伸ばす。
    //   戻り値は葉ステート（実際に再生する対象）。path には target 以降の hash を積む。
    const AnimState *EnterResolveLeaf(const AnimStateGraph &graph, uint32_t targetHash,
                                      std::vector<uint32_t> &path)
    {
        const AnimState *st = FindDirectState(graph, targetHash);
        if (!st) return nullptr;
        path.push_back(targetHash);
        while (st->isSubMachine && st->subGraph)
        {
            const AnimState *child = FindDirectState(*st->subGraph, st->subGraph->entryStateHash);
            if (!child) break; // entry 未設定などの安全策
            path.push_back(child->stateHash);
            st = child;
        }
        return st;
    }

    // hash のステートを階層のどこかから探し、root からの完全パスを out に構築（葉まで解決）。
    //   currentStateHash から activePath を作り直す時（初期化・エディタジャンプ）に使う。
    const AnimState *BuildPathTo(const AnimStateGraph &g, uint32_t hash, std::vector<uint32_t> &out)
    {
        for (const auto &s : g.states)
        {
            if (s.stateHash == hash)
            {
                const AnimState *st = &s;
                out.push_back(hash);
                while (st->isSubMachine && st->subGraph) // サブマシンを指していたら葉へ
                {
                    const AnimState *child = FindDirectState(*st->subGraph, st->subGraph->entryStateHash);
                    if (!child) break;
                    out.push_back(child->stateHash);
                    st = child;
                }
                return st;
            }
            if (s.isSubMachine && s.subGraph)
            {
                out.push_back(s.stateHash);
                if (const AnimState *r = BuildPathTo(*s.subGraph, hash, out)) return r;
                out.pop_back();
            }
        }
        return nullptr;
    }

    // 条件式1本の判定。結合（AND/OR）は呼び出し側が行う。
    bool EvaluateCondition(const AnimatorComponent &animator, const AnimParametersComponent &params,
                           const AnimCondition &cond)
    {
        if (cond.op == AnimConditionOp::Trigger)
            return params.HasTrigger(cond.paramHash);

        if (cond.op == AnimConditionOp::Finished)
            return animator.isFinished;

        float v = 0.0f;
        params.TryGetNumeric(cond.paramHash, v);
        switch (cond.op)
        {
        case AnimConditionOp::Greater:
            return v > cond.threshold;
        case AnimConditionOp::Less:
            return v < cond.threshold;
        case AnimConditionOp::Equal:
            return std::abs(v - cond.threshold) <= 0.0001f;
        default:
            return true;
        }
    }

    // 1本の遷移（キャンセル窓＋条件式）が満たされるか。
    //   文脈依存の「自己遷移スキップ」「グループスコープ」判定は呼び出し側で行う。
    //
    // 条件式の結合は t.logicType（AND / OR）に従う。エディタには
    // 「すべて満たす(AND) / いずれかを満たす(OR)」のドロップダウンがあるのに、
    // ここが logicType を読まず常に AND で畳んでいたため OR が無言で効いていなかった。
    //
    // キャンセル窓(requiredCancelMask)は OR の対象に含めない。窓は「この遷移を
    // 受け付けてよい時間帯か」というゲートであり、条件式の一項目ではないため、
    // OR を選んでも窓の外で遷移が通ってはいけない。
    bool ConditionsMet(const AnimatorComponent &animator, const AnimParametersComponent &params,
                       const AnimTransition &t)
    {
        if (!IsInCancelWindow(animator, t.requiredCancelMask)) return false;

        // 条件ゼロは「無条件」。AND/OR どちらでも成立とする（OR で false にすると
        // 条件を消しただけで遷移が死ぬ）。
        if (t.conditions.empty()) return true;

        if (t.logicType == AnimTransitionLogic::OR)
        {
            for (const auto &cond : t.conditions)
                if (EvaluateCondition(animator, params, cond)) return true;
            return false;
        }

        for (const auto &cond : t.conditions)
            if (!EvaluateCondition(animator, params, cond)) return false;
        return true;
    }

    // 遷移を実行：target を葉まで解決し、成功したらトリガーを消費して path を張り替え、葉を再生。
    //   graph は「遷移が発火した階層」（target はこの graph の兄弟ステート）。
    //
    // @return 実際に遷移したか。false のとき fsm / params は【一切変更していない】。
    //
    // 順序が重要。「トリガー消費 → path 切り詰め → 解決」の順にしてはいけない。解決に
    // 失敗すると "トリガーは消えた・path は壊れた・ステートは変わらない" で抜ける。
    // プレイヤーから見ると「ボタンを押したのに何も起きず、入力だけ消える」で、しかも
    // 壊れた path は次フレームの再構築が黙って直すので痕跡が残らない。
    // 失敗しうる処理を先に済ませ、確定してから状態を触ること。
    bool FireTransition(const AnimStateGraph &graph, int level, const AnimTransition &t,
                        AnimStateMachineComponent &fsm, AnimatorComponent &animator,
                        AnimParametersComponent &params, bool enableLog)
    {
        // --- ここまでは副作用なし（失敗しても何も壊さない） ---
        // EnterResolveLeaf が nullptr を返す唯一の経路が「target がこの階層に居ない」なので、
        // 同じ判定を先出しすれば path を触る前に失敗を検出できる。
        if (!FindDirectState(graph, t.targetStateHash))
        {
            // 無言で捨てるとステートを消した後の残存リンクに気付けない。
            CCL_LOG_WARN(LogCategory::Game,
                         "[AnimController] 遷移先のステート(hash=%u)がこの階層に見つかりません。遷移を中止します",
                         t.targetStateHash);
            return false;
        }

        // --- ここから確定。状態を変更する ---
        for (const auto &cond : t.conditions)
        {
            // OR 結合では「成立していない条件」も並ぶため、実際に立っているトリガーだけ消費する。
            //   全部消すと、この遷移に寄与していない先行入力まで巻き添えで消える。
            //   AND のときは全条件が成立済み＝全トリガーが立っているので従来と同じ挙動になる。
            if (cond.op == AnimConditionOp::Trigger && params.HasTrigger(cond.paramHash))
                params.ConsumeTrigger(cond.paramHash);
        }

        fsm.activePath.resize(static_cast<size_t>(level)); // この階層より内側を捨てる
        const AnimState *leaf = EnterResolveLeaf(graph, t.targetStateHash, fsm.activePath);
        fsm.currentStateHash  = fsm.activePath.back();

        if (leaf->sequence)
        {
            animator.Play(leaf->sequence,
                          {.blendTime              = t.blendDuration,
                           .loop                   = leaf->isLoop,
                           .speed                  = leaf->playbackSpeed,
                           .curve                  = leaf->speedCurve,
                           .rootMotionCurve        = leaf->rootMotionCurve,
                           .lockVerticalRootMotion = leaf->lockVerticalRootMotion,
                           // ステートに「入る」ので必ず頭から再生する。ここを渡さないと、別ステートでも
                           // クリップが同一だと Play が何もせず、再生位置・isFinished・イベント栞が
                           // 前のステートから引き継がれる（例: HitFront と HitBack は同じクリップ）。
                           .restart = true});
        }
        else
        {
            // クリップ未設定のステートへ入ると、FSM は新ステートのつもり・Animator は
            // 古いクリップを再生し続ける、という無言の食い違いになる。必ず気付けるようにする。
            CCL_LOG_WARN(LogCategory::Game,
                         "[AnimController] ステート[%s]にクリップが設定されていません（再生は前のまま）",
                         leaf->sequenceName.c_str());
        }

        if (enableLog)
            CCL_LOG_SUCCESS(LogCategory::Game, "[AnimController] -> [%s] (depth=%d)",
                            leaf->sequenceName.c_str(), (int)fsm.activePath.size());
        return true;
    }

    // level 階層を評価。発火して path/再生を更新したら true（このフレームの評価は打ち切り）。
    bool EvaluateLevel(const AnimStateGraph &graph, int level, AnimStateMachineComponent &fsm,
                       AnimatorComponent &animator, AnimParametersComponent &params, bool enableLog)
    {
        const uint32_t   activeHash  = (static_cast<size_t>(level) < fsm.activePath.size())
                                           ? fsm.activePath[level]
                                           : 0u;
        const AnimState *activeState = FindDirectState(graph, activeHash);
        const uint32_t   activeGroup = activeState ? activeState->groupId : 0u;

        // ① このグラフの anyState（グローバル＋グループ限定）＝外側の割り込みを最優先で見る
        for (const auto &t : graph.anyStateTransitions)
        {
            if (t.targetStateHash == activeHash) continue;                        // 自己遷移は無視
            if (t.sourceGroupId != 0 && t.sourceGroupId != activeGroup) continue; // グループ限定
            if (!ConditionsMet(animator, params, t)) continue;
            // 発火に失敗（遷移先が壊れている）なら、その1本を諦めて次の候補を見る
            if (FireTransition(graph, level, t, fsm, animator, params, enableLog)) return true;
        }

        if (!activeState) return false;

        if (activeState->isSubMachine && activeState->subGraph)
        {
            // ② サブマシン自身の out 遷移＝親レベルの出口（例: Combat 全体 → Locomotion）
            for (const auto &t : activeState->transitions)
            {
                if (!ConditionsMet(animator, params, t)) continue;
                if (FireTransition(graph, level, t, fsm, animator, params, enableLog)) return true;
            }
            // ③ 子グラフへ降りて内側を評価（外側で発火しなかった時だけ）
            return EvaluateLevel(*activeState->subGraph, level + 1, fsm, animator, params, enableLog);
        }

        // 葉：自身の遷移を評価
        for (const auto &t : activeState->transitions)
        {
            if (!ConditionsMet(animator, params, t)) continue;
            if (FireTransition(graph, level, t, fsm, animator, params, enableLog)) return true;
        }
        return false;
    }

    // グラフ内の全ステートの sequence/curve を束縛（サブマシンの子も再帰的に）。
    void BindGraphResources(AnimStateGraph &g, ResourceManager &rm)
    {
        for (auto &state : g.states)
        {
            if (!state.sequenceFilePath.empty())
                state.sequence = rm.LoadAnimSequence(state.sequenceFilePath.c_str());
            if (!state.speedCurveName.empty())
                state.speedCurve = rm.LoadAnimCurve(state.speedCurveName.c_str());
            if (!state.rootMotionCurveName.empty())
                state.rootMotionCurve = rm.LoadAnimCurve(state.rootMotionCurveName.c_str());
            if (state.isSubMachine && state.subGraph)
                BindGraphResources(*state.subGraph, rm); // 子グラフも束縛
        }
    }
} // namespace

// ステートマシン評価。ロード/束縛（シリアル）と評価（並列）を2フェーズに分離する。
//
// フェーズ1で全ポインタ(sequence/speedCurve)を束縛し、フェーズ2の並列評価から
// ResourceManager（共有シングルトン）・ファイルI/Oを完全に排除してレースを防ぐ。
void AnimControllerSystem::Update(float dt)
{
    ZoneScopedN("AnimControllerSystem::Update");

    auto &resourceManager = ResourceManager::Instance();

    // =====================================================================
    // フェーズ1【シリアル】グラフのロード・ポインタ束縛・初期再生
    //   ファイルI/O と ResourceManager 変更はスレッド非安全のため、並列ループから
    //   分離し必ずメインスレッドで実行する。定常状態では states が非空なので、
    //   この走査は先頭の早期 return でほぼ分岐1回だけで抜ける（実質ノーコスト）。
    // =====================================================================
    ForEachWithID([&resourceManager](CCL::ECS::EntityID /*id*/,
                                     AnimStateMachineComponent &fsm,
                                     AnimatorComponent         &animator,
                                     AnimParametersComponent & /*params*/,
                                     const TimeStateComponent & /*time*/) {
        if (animator.isEditorOverride)
            return;

        // 未ロードのグラフのみ処理（ロード済み or パス未設定なら即 return）
        if (!fsm.internalGraph.states.empty() || fsm.graphPath.empty())
            return;

        // ここに来るのは初回のみ（グラフJSONロード＋シーケンス/カーブのディスクロード）。
        // 起動直後は全キャラ分がこのフレームに集中するため、初回スパイクの正体はここ。
        ZoneScopedN("AnimCtrl: LoadGraph+Bind (first frame)");

        if (!AnimGraphSerializer::LoadFromJSON(fsm.internalGraph, fsm.graphPath, {}))
        {
            fsm.graphPath = ""; // ロード失敗時はクリア（毎フレーム再試行しない）
            return;
        }

        // 全ステートの sequence/curve を束縛（サブマシンの子も再帰的に）。
        //   これで並列評価フェーズ(2)から ResourceManager を呼ぶ必要がなくなる。
        BindGraphResources(fsm.internalGraph, resourceManager);

        // 初期状態：entry へ入り、サブマシンなら葉まで潜って activePath を構築する。
        fsm.activePath.clear();
        const AnimState *leaf = EnterResolveLeaf(fsm.internalGraph,
                                                 fsm.internalGraph.entryStateHash, fsm.activePath);
        fsm.currentStateHash  = fsm.activePath.empty() ? fsm.internalGraph.entryStateHash
                                                       : fsm.activePath.back();

        // 初期再生のブートストラップ（最初の再生なのでブレンドは0）
        if (leaf && leaf->sequence)
        {
            animator.Play(leaf->sequence,
                          {.blendTime              = 0.0f,
                           .loop                   = leaf->isLoop,
                           .speed                  = leaf->playbackSpeed,
                           .curve                  = leaf->speedCurve,
                           .rootMotionCurve        = leaf->rootMotionCurve,
                           .lockVerticalRootMotion = leaf->lockVerticalRootMotion});
        }
    });

    // ログフラグをローカルにコピーし、ラムダ式にキャプチャさせてスレッドセーフにする
    const bool enableLog = m_enableTransitionLog;

    // =====================================================================
    // フェーズ2【並列】純粋なステートマシン評価
    //   ResourceManager / ファイルI/O は一切呼ばない（フェーズ1で束縛済み）。
    // =====================================================================
    ForEachWithIDParallel([enableLog](CCL::ECS::EntityID         id,
                                      AnimStateMachineComponent &fsm,
                                      AnimatorComponent         &animator,
                                      AnimParametersComponent   &params,
                                      const TimeStateComponent  &time) { // TimeStateComponent を追加
        if (animator.isEditorOverride) return;

        // コアロジック: ステート評価の前に、先行入力バッファの寿命を削ぎ落とす
        // time.localDt を使うことで、ヒットストップやスローモーション中も完璧に同期する
        params.TickTriggers(time.localDt);

        if (fsm.internalGraph.states.empty()) return;

        // ====================================================================
        // 階層評価：activePath を currentStateHash と整合させてから、外→内に評価する
        // ====================================================================

        // 初期化直後・エディタからのジャンプ等で activePath が空/末尾不一致なら作り直す。
        //   まず currentStateHash を起点に階層パスを構築、見つからなければ entry から再構築。
        if (fsm.activePath.empty() || fsm.activePath.back() != fsm.currentStateHash)
        {
            fsm.activePath.clear();
            const AnimState *cur = BuildPathTo(fsm.internalGraph, fsm.currentStateHash, fsm.activePath);
            if (!cur)
            {
                fsm.activePath.clear();
                cur = EnterResolveLeaf(fsm.internalGraph, fsm.internalGraph.entryStateHash, fsm.activePath);
                if (!cur) return; // グラフが壊れている等の安全策
            }
            if (!fsm.activePath.empty())
                fsm.currentStateHash = fsm.activePath.back();
        }

        // 階層を root→葉へ評価。どこか1階層で遷移が発火したらそのフレームは打ち切り。
        //   外側(親)の割り込みが内側(子)より優先される（EvaluateLevel が先に親を見る）。
        EvaluateLevel(fsm.internalGraph, 0, fsm, animator, params, enableLog);
    });
}

// システムのインスペクタ表示
void AnimControllerSystem::OnGui()
{
    ImGui::Text("Animation Controller Settings");
    ImGui::Separator();

    ImGui::Checkbox("Enable Transition Logs", &m_enableTransitionLog);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("チェックを入れると、ステートが切り替わった瞬間にコンソールへログを出力します。\n高速遷移によるログ汚染を防ぐため、デバッグ時のみONにしてください。");
    }
}

REGISTER_LOGIC_SYSTEM(AnimControllerSystem, Priority::LogicStage::L02_Update);
