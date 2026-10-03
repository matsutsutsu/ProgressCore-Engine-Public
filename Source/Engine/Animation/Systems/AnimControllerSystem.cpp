#include "AnimControllerSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <imgui.h>
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Animation/Core/AnimGraphSerializer.h"
#include "Engine/Platform/Core/Logger.h"
#include <tracy/Tracy.hpp>

// 再生中クリップの ComboWindow / CancelWindow のうち、requiredMask とビットが重なる区間の中にいるか
static bool IsInCancelWindow(const AnimatorComponent &animator, uint32_t requiredMask)
{
    if (requiredMask == 0)
        return true; // マスク指定なしなら常に許可

    const AnimSequence *seq = animator.currentSequence;
    if (!seq)
        return false;

    for (const auto &ev : seq->events)
    {
        if (ev.type != AnimEventType::ComboWindow &&
            ev.type != AnimEventType::CancelWindow)
            continue;

        if ((ev.cancelMask & requiredMask) == 0)
            continue;

        if (animator.currentTimer >= ev.startTime &&
            animator.currentTimer <= ev.endTime)
            return true;
    }
    return false;
}

// ===========================================================================
// 階層ステートマシンの評価
//   現在地は fsm.activePath [ルート側..., 葉]。各階層で
//     1. このグラフの anyState（外側からの割り込み）
//     2. 今のステートがサブマシンなら、その遷移（サブマシン全体の出口）
//     3. 子グラフへ降りる
//   の順に見るので、外側（親）の遷移が内側（子）より優先される。
// ===========================================================================
namespace
{
    // このグラフの直下のステートを hash で引く（子グラフはたどらない）
    const AnimState *FindDirectState(const AnimStateGraph &g, uint32_t hash)
    {
        for (const auto &s : g.states)
            if (s.stateHash == hash) return &s;
        return nullptr;
    }

    /**
     * @brief graph 内の target に入り、サブマシンなら entry へ降りて葉まで path を伸ばす。
     * @param graph      探すグラフ。
     * @param targetHash 入るステートの名前のハッシュ。
     * @param path       target 以降の hash を積む先。
     * @return 葉ステート（実際に再生する対象）。target が無ければ nullptr。
     */
    const AnimState *EnterResolveLeaf(const AnimStateGraph &graph, uint32_t targetHash,
                                      std::vector<uint32_t> &path)
    {
        const AnimState *st = FindDirectState(graph, targetHash);
        if (!st) return nullptr;
        path.push_back(targetHash);
        while (st->isSubMachine && st->subGraph)
        {
            const AnimState *child = FindDirectState(*st->subGraph, st->subGraph->entryStateHash);
            if (!child) break; // entry が未設定
            path.push_back(child->stateHash);
            st = child;
        }
        return st;
    }

    /**
     * @brief hash のステートを階層全体から探し、ルートから葉までのパスを out に作る。
     * @param g    探すグラフ。
     * @param hash 探すステートの名前のハッシュ。
     * @param out  ルートからのパスを積む先。
     * @return 見つかったステート。無ければ nullptr。
     * @note currentStateHash から activePath を作り直すとき（初期化・エディタからのジャンプ）に使う。
     */
    const AnimState *BuildPathTo(const AnimStateGraph &g, uint32_t hash, std::vector<uint32_t> &out)
    {
        for (const auto &s : g.states)
        {
            if (s.stateHash == hash)
            {
                const AnimState *st = &s;
                out.push_back(hash);
                while (st->isSubMachine && st->subGraph) // サブマシンなら葉まで降りる
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

    // 条件 1 つの判定。AND/OR の結合は呼び出し側が行う
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

    /**
     * @brief 遷移 1 本（受付区間 + 条件）が満たされるか。
     * @note 条件は t.logicType（AND / OR）で結合する。
     *       受付区間（requiredCancelMask）は OR の対象に含めない。受付区間は「この遷移を受け付けてよい
     *       時間帯か」という前提で、条件の 1 つではないので、OR でも区間の外では遷移させない。
     *       自己遷移の除外とグループの判定は呼び出し側で行う。
     */
    bool ConditionsMet(const AnimatorComponent &animator, const AnimParametersComponent &params,
                       const AnimTransition &t)
    {
        if (!IsInCancelWindow(animator, t.requiredCancelMask)) return false;

        // 条件が無ければ無条件で成立（OR で false にすると、条件を消しただけで遷移しなくなる）
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

    /**
     * @brief 遷移を実行する。target を葉まで解決し、トリガーを消費して path を張り替え、葉を再生する。
     * @param graph 遷移が発火した階層（target はこの graph の直下のステート）
     * @return 実際に遷移したか。false のとき fsm / params は一切変更していない
     * @note 失敗しうる判定を先に済ませ、確定してから状態を変えること。
     *       「トリガー消費 → path 切り詰め → 解決」の順にすると、解決に失敗したとき
     *       トリガーだけ消えてステートは変わらない（ボタンを押したのに入力だけ消える）。
     *       壊れた path は次のステップの再構築で直ってしまうので、痕跡も残らない。
     */
    bool FireTransition(const AnimStateGraph &graph, int level, const AnimTransition &t,
                        AnimStateMachineComponent &fsm, AnimatorComponent &animator,
                        AnimParametersComponent &params, bool enableLog)
    {
        // ここまでは状態を変えない。EnterResolveLeaf が nullptr を返すのは
        // target がこの階層に無いときだけなので、同じ判定を先に行えば path を触る前に失敗を検出できる
        if (!FindDirectState(graph, t.targetStateHash))
        {
            // 黙って捨てると、ステートを消した後に残った遷移に気付けない
            CCL_LOG_WARN(LogCategory::Game,
                         "[AnimController] 遷移先のステート(hash=%u)がこの階層に見つかりません。遷移を中止します",
                         t.targetStateHash);
            return false;
        }

        // ここから状態を変更する
        for (const auto &cond : t.conditions)
        {
            // OR では成立していない条件も並ぶので、実際に立っているトリガーだけ消費する。
            //   全て消すと、この遷移に関係ない先行入力まで消える
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
                           // ステートに入るので必ず頭から再生する（理由は PlayParams::restart を参照）
                           .restart = true});
        }
        else
        {
            // クリップ未設定のステートに入ると、ステートは変わったのに前のクリップが再生され続ける。
            // 気付けるよう警告を出す
            CCL_LOG_WARN(LogCategory::Game,
                         "[AnimController] ステート[%s]にクリップが設定されていません（再生は前のまま）",
                         leaf->sequenceName.c_str());
        }

        if (enableLog)
            CCL_LOG_SUCCESS(LogCategory::Game, "[AnimController] -> [%s] (depth=%d)",
                            leaf->sequenceName.c_str(), (int)fsm.activePath.size());
        return true;
    }

    // level 階層を評価する。遷移したら true（このステップの評価はそこで終わる）
    bool EvaluateLevel(const AnimStateGraph &graph, int level, AnimStateMachineComponent &fsm,
                       AnimatorComponent &animator, AnimParametersComponent &params, bool enableLog)
    {
        const uint32_t   activeHash  = (static_cast<size_t>(level) < fsm.activePath.size())
                                           ? fsm.activePath[level]
                                           : 0u;
        const AnimState *activeState = FindDirectState(graph, activeHash);
        const uint32_t   activeGroup = activeState ? activeState->groupId : 0u;

        // 1. このグラフの anyState（全体 + グループ限定）を最優先で見る
        for (const auto &t : graph.anyStateTransitions)
        {
            if (t.targetStateHash == activeHash) continue;                        // 自己遷移は無視する
            if (t.sourceGroupId != 0 && t.sourceGroupId != activeGroup) continue; // 別グループ限定の遷移
            if (!ConditionsMet(animator, params, t)) continue;
            // 遷移先が見つからず失敗したら、その 1 本は諦めて次の候補を見る
            if (FireTransition(graph, level, t, fsm, animator, params, enableLog)) return true;
        }

        if (!activeState) return false;

        if (activeState->isSubMachine && activeState->subGraph)
        {
            // 2. サブマシン自身の遷移（サブマシン全体の出口。例: Combat 全体 → Locomotion）
            for (const auto &t : activeState->transitions)
            {
                if (!ConditionsMet(animator, params, t)) continue;
                if (FireTransition(graph, level, t, fsm, animator, params, enableLog)) return true;
            }
            // 3. 外側で遷移しなかったときだけ、子グラフへ降りて評価する
            return EvaluateLevel(*activeState->subGraph, level + 1, fsm, animator, params, enableLog);
        }

        // 葉: 自身の遷移を評価する
        for (const auto &t : activeState->transitions)
        {
            if (!ConditionsMet(animator, params, t)) continue;
            if (FireTransition(graph, level, t, fsm, animator, params, enableLog)) return true;
        }
        return false;
    }

    // グラフ内の全ステートの sequence / curve を束縛する（サブマシンの子も再帰的に）
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
                BindGraphResources(*state.subGraph, rm);
        }
    }
} // namespace

void AnimControllerSystem::Update(float dt)
{
    ZoneScopedN("AnimControllerSystem::Update");

    auto &resourceManager = ResourceManager::Instance();

    // 1 段目（直列）: グラフの読込・ポインタの束縛・初回の再生。
    //   ファイル I/O と ResourceManager の変更はスレッドセーフでないので、並列ループから外す。
    //   読込済みなら先頭の早期 return で抜けるので、普段はほぼコストが無い
    ForEachWithID([&resourceManager](CCL::ECS::EntityID /*id*/,
                                     AnimStateMachineComponent &fsm,
                                     AnimatorComponent         &animator,
                                     AnimParametersComponent & /*params*/,
                                     const TimeStateComponent & /*time*/) {
        if (animator.isEditorOverride)
            return;

        // 未読込のグラフだけ処理する（読込済みかパス未設定なら何もしない）
        if (!fsm.internalGraph.states.empty() || fsm.graphPath.empty())
            return;

        // ここに来るのは初回だけ。起動直後は全キャラ分がこのフレームに集中し、処理が一時的に重くなる
        ZoneScopedN("AnimCtrl: LoadGraph+Bind (first frame)");

        if (!AnimGraphSerializer::LoadFromJSON(fsm.internalGraph, fsm.graphPath))
        {
            fsm.graphPath = ""; // 失敗したらパスを消し、毎フレーム再試行しない
            return;
        }

        // ここで束縛しておけば、2 段目の並列評価で ResourceManager を呼ばずに済む
        BindGraphResources(fsm.internalGraph, resourceManager);

        // entry に入り、サブマシンなら葉まで降りて activePath を作る
        fsm.activePath.clear();
        const AnimState *leaf = EnterResolveLeaf(fsm.internalGraph,
                                                 fsm.internalGraph.entryStateHash, fsm.activePath);
        fsm.currentStateHash  = fsm.activePath.empty() ? fsm.internalGraph.entryStateHash
                                                       : fsm.activePath.back();

        // 最初の再生なのでブレンドしない
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

    // 並列のラムダからメンバを読まないよう、ローカルにコピーしてキャプチャする
    const bool enableLog = m_enableTransitionLog;

    // 2 段目（並列）: ステートマシンの評価。ResourceManager / ファイル I/O は呼ばない
    ForEachWithIDParallel([enableLog](CCL::ECS::EntityID         id,
                                      AnimStateMachineComponent &fsm,
                                      AnimatorComponent         &animator,
                                      AnimParametersComponent   &params,
                                      const TimeStateComponent  &time) {
        if (animator.isEditorOverride) return;

        // 評価の前に先行入力の寿命を進める。localDt を使うので、ヒットストップやスロー中は寿命も遅く減る
        params.TickTriggers(time.localDt);

        if (fsm.internalGraph.states.empty()) return;

        // 初期化直後やエディタからのジャンプで activePath が空か末尾が食い違っていたら作り直す。
        //   まず currentStateHash からパスを作り、見つからなければ entry から作り直す
        if (fsm.activePath.empty() || fsm.activePath.back() != fsm.currentStateHash)
        {
            fsm.activePath.clear();
            const AnimState *cur = BuildPathTo(fsm.internalGraph, fsm.currentStateHash, fsm.activePath);
            if (!cur)
            {
                fsm.activePath.clear();
                cur = EnterResolveLeaf(fsm.internalGraph, fsm.internalGraph.entryStateHash, fsm.activePath);
                if (!cur) return; // entry も見つからない（グラフが壊れている）
            }
            if (!fsm.activePath.empty())
                fsm.currentStateHash = fsm.activePath.back();
        }

        // ルートから葉へ評価する。どこかの階層で遷移したら、このステップはそこで終わる
        EvaluateLevel(fsm.internalGraph, 0, fsm, animator, params, enableLog);
    });
}

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
