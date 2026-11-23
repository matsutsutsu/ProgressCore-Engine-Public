#include "AnimControllerSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <imgui.h>
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Platform/Core/Logger.h"
#include <tracy/Tracy.hpp>

// ---------------------------------------------------------------------------
// 階層の探索
// ---------------------------------------------------------------------------

const AnimState *AnimControllerSystem::DescendToLeaf(const AnimState *st, std::vector<uint32_t> &path)
{
    while (st->isSubMachine && st->subGraph)
    {
        const AnimState *child = FindDirectState(*st->subGraph, st->subGraph->entryStateHash);
        if (!child) break; // entry が未設定
        path.push_back(child->stateHash);
        st = child;
    }
    return st;
}



const AnimState *AnimControllerSystem::EnterResolveLeaf(const AnimStateGraph &graph, uint32_t targetHash,
                                                        std::vector<uint32_t> &path)
{
    const AnimState *st = FindDirectState(graph, targetHash);
    if (!st) return nullptr;

    path.push_back(targetHash);
    return DescendToLeaf(st, path);
}



const AnimState *AnimControllerSystem::BuildPathTo(const AnimStateGraph &graph, uint32_t hash,
                                                   std::vector<uint32_t> &out)
{
    // この階層にあれば、そこから葉まで降りる
    if (const AnimState *st = EnterResolveLeaf(graph, hash, out)) return st;

    // なければサブマシンの中を探す。見つからなかった枝は積んだ分を戻す
    for (const auto &s : graph.states)
    {
        if (!s.isSubMachine || !s.subGraph) continue;

        out.push_back(s.stateHash);
        if (const AnimState *r = BuildPathTo(*s.subGraph, hash, out)) return r;
        out.pop_back();
    }
    return nullptr;
}



// ---------------------------------------------------------------------------
// 条件の判定
// ---------------------------------------------------------------------------

bool AnimControllerSystem::EvaluateCondition(const AnimatorComponent &animator, const AnimParametersComponent &params,
                                             const AnimCondition &cond)
{
    if (cond.op == AnimConditionOp::Trigger) return params.HasTrigger(cond.paramHash);
    if (cond.op == AnimConditionOp::Finished) return animator.isFinished;

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



bool AnimControllerSystem::ConditionsMet(const AnimatorComponent &animator, const AnimParametersComponent &params,
                                         const AnimTransition &t)
{
    // 受付区間は OR の対象に含めない
    //   「この遷移を受け付けてよい時間帯か」という前提なので、OR でも区間の外では遷移させない
    const AnimSequence *seq        = animator.currentSequence;
    const bool          windowOpen = (t.requiredCancelMask == 0) ||
                            (seq && IsInCancelWindow(*seq, t.requiredCancelMask, animator.currentTimer));
    if (!windowOpen) return false;

    // 条件がなければ無条件で成立する
    //   OR で false にすると、条件を消しただけで遷移しなくなる
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



// ---------------------------------------------------------------------------
// 遷移の実行と階層の評価
// ---------------------------------------------------------------------------

const AnimSequence *AnimControllerSystem::StateSequence(const AnimState &state)
{
    if (!state.isBlendSpace) return state.sequence;
    return state.blendSpace.samples.empty() ? nullptr : state.blendSpace.samples[0].sequence;
}



void AnimControllerSystem::PlayState(AnimatorComponent &animator, const AnimState &leaf, float blendTime, bool restart)
{
    animator.Play(StateSequence(leaf),
                  {.blendTime              = blendTime,
                   .loop                   = leaf.isLoop,
                   .speed                  = leaf.playbackSpeed,
                   .lockVerticalRootMotion = leaf.lockVerticalRootMotion,
                   .restart                = restart,
                   .blendSpace             = leaf.isBlendSpace ? &leaf.blendSpace : nullptr});
}



void AnimControllerSystem::UpdateBlendWeights(AnimatorComponent &animator, const AnimParametersComponent &params,
                                              float dt)
{
    AnimBlendPlayback &blend = animator.currentBlend;
    if (!blend.IsActive()) return;

    float targetX = 0.0f;
    float targetY = 0.0f;
    params.TryGetNumeric(blend.paramXHash, targetX);
    params.TryGetNumeric(blend.paramYHash, targetY);

    // 入った直後は寄せずに目標から始める
    //   0 から寄せると、入った瞬間だけ別の向きの姿勢が混ざる
    if (!blend.hasInput)
    {
        blend.inputX   = targetX;
        blend.inputY   = targetY;
        blend.hasInput = true;
    }
    else
    {
        blend.inputX = CCL::Anim::SmoothToward(blend.inputX, targetX, blend.smoothingTime, dt);
        blend.inputY = CCL::Anim::SmoothToward(blend.inputY, targetY, blend.smoothingTime, dt);
    }

    CCL::Anim::ComputeBlendWeights(blend.type, blend.xs.data(), blend.ys.data(), blend.sampleCount,
                                   blend.inputX, blend.inputY, blend.weights.data());
}



bool AnimControllerSystem::FireTransition(const AnimStateGraph &graph, int level, const AnimTransition &t,
                                          AnimStateMachineComponent &fsm, AnimatorComponent &animator,
                                          AnimParametersComponent &params, bool enableLog)
{
    // 処理フロー:
    // 1. 遷移先があるかを確かめる（ここまでは状態を変えない）
    // 2. 使ったトリガーを消費する
    // 3. 現在地を張り替え、葉を再生する

    // 失敗しうる判定を先に済ませ、確定してから状態を変える
    // 順序を逆にすると、遷移先がないときにトリガーだけ消える（押したのに入力だけ消える）
    // 壊れた path は次のステップの再構築で直ってしまうので、痕跡も残らない

    // ---------------------------------------------------------
    // 1. 遷移先の確認
    // ---------------------------------------------------------

    // EnterResolveLeaf が nullptr を返すのは target がこの階層にないときだけなので、
    //   同じ判定を先に行えば path を触る前に失敗を検出できる
    if (!FindDirectState(graph, t.targetStateHash))
    {
        // 黙って捨てると、ステートを消した後に残った遷移に気付けない
        CCL_LOG_WARN(LogCategory::Game,
                     "[AnimController] 遷移先のステート(hash=%u)がこの階層に見つかりません。遷移を中止します",
                     t.targetStateHash);
        return false;
    }

    // ---------------------------------------------------------
    // 2. トリガーの消費
    // ---------------------------------------------------------
    for (const auto &cond : t.conditions)
    {
        // 実際に立っているトリガーだけ消費する
        //   OR では成立していない条件も並ぶので、すべて消すと関係ない先行入力まで消える
        if (cond.op == AnimConditionOp::Trigger && params.HasTrigger(cond.paramHash))
            params.ConsumeTrigger(cond.paramHash);
    }

    // ---------------------------------------------------------
    // 3. 現在地の張り替えと再生
    // ---------------------------------------------------------
    fsm.activePath.resize(static_cast<size_t>(level)); // この階層より内側を捨てる
    const AnimState *leaf = EnterResolveLeaf(graph, t.targetStateHash, fsm.activePath);

    if (StateSequence(*leaf))
    {
        // ステートに入るので必ず頭から再生する（理由は AnimatorComponent::Play を参照）
        PlayState(animator, *leaf, t.blendDuration, true);
    }
    else
    {
        // クリップ未設定のステートに入ると、ステートは変わったのに前のクリップが流れ続ける
        //   気付けるよう警告を出す
        CCL_LOG_WARN(LogCategory::Game,
                     "[AnimController] ステート[%s]にクリップが設定されていません（再生は前のまま）",
                     leaf->stateName.c_str());
    }

    if (enableLog)
        CCL_LOG_SUCCESS(LogCategory::Game, "[AnimController] -> [%s] (depth=%d)",
                        leaf->stateName.c_str(), (int)fsm.activePath.size());
    return true;
}



bool AnimControllerSystem::EvaluateLevel(const AnimStateGraph &graph, int level, AnimStateMachineComponent &fsm,
                                         AnimatorComponent &animator, AnimParametersComponent &params,
                                         bool enableLog)
{
    // 処理フロー:
    // 1. この階層の AnyState を見る
    // 2. 今のステートがサブマシンなら、その出口の遷移を見てから子の階層へ降りる
    // 3. 葉なら、自身の遷移を見る

    // 外側（親）の遷移を内側（子）より先に見るので、外側が常に優先される
    // 例: Atk_Combo の中で攻撃中でも、root の AnyState → Hit_Reaction が先に通る

    const uint32_t   activeHash  = (static_cast<size_t>(level) < fsm.activePath.size())
                                       ? fsm.activePath[level]
                                       : 0u;
    const AnimState *activeState = FindDirectState(graph, activeHash);
    const uint32_t   activeGroup = activeState ? activeState->groupId : 0u;

    // ---------------------------------------------------------
    // 1. AnyState（全体 + グループ限定）
    // ---------------------------------------------------------
    for (const auto &t : graph.anyStateTransitions)
    {
        if (t.targetStateHash == activeHash) continue;                        // 自己遷移は無視する
        if (t.sourceGroupId != 0 && t.sourceGroupId != activeGroup) continue; // 別グループ限定の遷移
        if (!ConditionsMet(animator, params, t)) continue;

        // 遷移先が見つからず失敗したら、その 1 本は諦めて次の候補を見る
        if (FireTransition(graph, level, t, fsm, animator, params, enableLog)) return true;
    }

    if (!activeState) return false;

    // ---------------------------------------------------------
    // 2. サブマシン（出口 → 子の階層）
    // ---------------------------------------------------------
    if (activeState->isSubMachine && activeState->subGraph)
    {
        // サブマシン自身の遷移は、サブマシン全体の出口（例: Atk_Combo 全体 → Loco_Idle）
        for (const auto &t : activeState->transitions)
        {
            if (!ConditionsMet(animator, params, t)) continue;
            if (FireTransition(graph, level, t, fsm, animator, params, enableLog)) return true;
        }

        // 外側で遷移しなかったときだけ、子の階層へ降りて評価する
        return EvaluateLevel(*activeState->subGraph, level + 1, fsm, animator, params, enableLog);
    }

    // ---------------------------------------------------------
    // 3. 葉
    // ---------------------------------------------------------
    for (const auto &t : activeState->transitions)
    {
        if (!ConditionsMet(animator, params, t)) continue;
        if (FireTransition(graph, level, t, fsm, animator, params, enableLog)) return true;
    }
    return false;
}



// ---------------------------------------------------------------------------
// 毎ステップの更新
// ---------------------------------------------------------------------------

void AnimControllerSystem::Update(float dt)
{
    // 処理フロー:
    // 1. まだ読んでいないグラフを読み込む（直列）
    // 2. ステートマシンを評価する（並列）

    // 2 段に分けるのは、ファイル I/O と ResourceManager がスレッドセーフでないため
    // グラフの受け取りを 1 段目で済ませておけば、2 段目は ResourceManager を呼ばずに済む

    ZoneScopedN("AnimControllerSystem::Update");

    // ---------------------------------------------------------
    // 1. グラフの読込（直列）
    // ---------------------------------------------------------

    // 読込済みなら先頭の早期 return で抜けるので、普段はほぼコストがない
    ForEachWithID([](CCL::ECS::EntityID /*id*/,
                     AnimStateMachineComponent &fsm,
                     AnimatorComponent         &animator,
                     AnimParametersComponent   &params,
                     const TimeStateComponent & /*time*/) {
        // 握られている間は読まない。読込で entry を再生すると、握っている側のクリップを上書きする
        //   握りが離れた後の最初のステップで entry に入る
        if (animator.IsOverridden()) return;

        // まだグラフを受け取っていない Entity だけ処理する（受け取り済みかパス未設定なら何もしない）
        if (fsm.graph || fsm.graphPath.empty()) return;

        // ここに来るのは初回だけ。同じグラフを使う Entity のうち、ファイルを読むのは最初の 1 体だけ
        ZoneScopedN("AnimCtrl: LoadGraph (first frame)");

        // 失敗したらパスを消し、毎フレーム再試行しない
        if (!LoadGraph(fsm, &animator, &params)) fsm.graphPath = "";
    });

    // ---------------------------------------------------------
    // 2. ステートマシンの評価（並列）
    // ---------------------------------------------------------

    // 並列のラムダからメンバを読まないよう、ローカルにコピーしてキャプチャする
    const bool enableLog = m_enableTransitionLog;

    ForEachWithIDParallel([enableLog](CCL::ECS::EntityID         id,
                                      AnimStateMachineComponent &fsm,
                                      AnimatorComponent         &animator,
                                      AnimParametersComponent   &params,
                                      const TimeStateComponent  &time) {
        if (animator.isEditorOverride) return;

        // --- 先行入力の寿命 ---

        // 評価の前に寿命を進める。localDt なので、ヒットストップやスロー中は寿命も遅く減る
        params.TickTriggers(time.localDt);

        if (!fsm.graph) return;

        // --- 現在地の確認 ---

        // 現在地がグラフの中でたどれなければ、entry から入り直す
        //   同じグラフを使う別の Entity がグラフを読み直し、いたステートがなくなった場合など
        if (!fsm.GetCurrentState()) EnterEntry(fsm, &animator);
        if (fsm.activePath.empty()) return; // entry も見つからない（グラフが壊れている）

        // --- ムービーの握り ---

        // 握られている間は遷移を評価しない。評価すると、ムービーのクリップを Idle などへ戻してしまう
        //   トリガーの寿命は上で進めているので、ムービー中の入力が明けてから暴発することもない
        if (animator.isExternalOverride) return;

        // 握りが離れた直後は、今のステートのクリップへブレンドして戻す
        //   握っている間もステートは変わっていないので、遷移ではなく「今のステートに入り直す」だけでよい
        if (animator.externalReleaseBlend >= 0.0f)
        {
            if (const AnimState *leaf = fsm.GetCurrentState(); leaf && StateSequence(*leaf))
            {
                PlayState(animator, *leaf, animator.externalReleaseBlend, true);
            }
            animator.externalReleaseBlend = -1.0f;
        }

        // --- 遷移の評価 ---

        // ルートから葉へ評価する。どこかの階層で遷移したら、このステップはそこで終わる
        EvaluateLevel(*fsm.graph, 0, fsm, animator, params, enableLog);

        // --- ブレンドスペースの重み ---

        // 遷移の後に求める。このステップでブレンドスペースに入っても、入ったステップから重みが揃う
        UpdateBlendWeights(animator, params, time.localDt);
    });
}



// ---------------------------------------------------------------------------
// 読込とジャンプ（初回の読込とインスペクタから呼ばれる）
// ---------------------------------------------------------------------------

bool AnimControllerSystem::LoadGraph(AnimStateMachineComponent &fsm, AnimatorComponent *animator,
                                     AnimParametersComponent *params)
{
    // 同じファイルを読み込み済みなら、その実体を受け取るだけでファイルは読まない
    const AnimStateGraph *graph = ResourceManager::Instance().LoadAnimGraph(fsm.graphPath.c_str());
    if (!graph) return false;

    StartGraph(fsm, *graph, animator, params);
    return true;
}



bool AnimControllerSystem::ReloadGraph(AnimStateMachineComponent &fsm, AnimatorComponent *animator,
                                       AnimParametersComponent *params)
{
    const AnimStateGraph *graph = ResourceManager::Instance().ReloadAnimGraph(fsm.graphPath.c_str());
    if (!graph) return false;

    StartGraph(fsm, *graph, animator, params);
    return true;
}



bool AnimControllerSystem::JumpToState(AnimStateMachineComponent &fsm, AnimatorComponent &animator, uint32_t stateHash)
{
    if (!fsm.graph) return false;

    // ルートからのパスを先に作り、見つかったときだけ現在地を差し替える
    std::vector<uint32_t> path;
    const AnimState      *leaf = BuildPathTo(*fsm.graph, stateHash, path);
    if (!leaf) return false;

    fsm.activePath = std::move(path);

    // 現在地だけを書き換えると、ステートは変わってもクリップが前のまま流れる
    //   握られている間は再生しない（握っている側のクリップを上書きしないため）
    if (animator.IsOverridden()) return true;
    if (StateSequence(*leaf)) PlayState(animator, *leaf, 0.0f, true);
    return true;
}



void AnimControllerSystem::StartGraph(AnimStateMachineComponent &fsm, const AnimStateGraph &graph,
                                      AnimatorComponent *animator, AnimParametersComponent *params)
{
    fsm.graph = &graph;

    // パラメータの初期値は、まだ値を持っていないパラメータにだけ入れる
    //   ゲーム側が先に書いた値（毎ステップ書く VelocityY など）を初期値で上書きしない
    if (params)
    {
        for (const auto &def : graph.parameters)
        {
            float current = 0.0f;
            if (params->TryGetNumeric(def.hash, current)) continue;

            if (def.type == AnimParamType::Float) params->SetFloat(def.hash, def.defaultFloat);
            else if (def.type == AnimParamType::Bool) params->SetBool(def.hash, def.defaultBool);
        }
    }

    // 再読込のときも entry の頭から流し直す
    EnterEntry(fsm, animator);
}



void AnimControllerSystem::EnterEntry(AnimStateMachineComponent &fsm, AnimatorComponent *animator)
{
    fsm.activePath.clear();
    const AnimState *leaf = EnterResolveLeaf(*fsm.graph, fsm.graph->entryStateHash, fsm.activePath);

    // 握られている間は再生しない（握っている側のクリップを上書きしないため）
    //   入り直しなのでブレンドしない
    if (!animator || animator->IsOverridden()) return;
    if (leaf && StateSequence(*leaf)) PlayState(*animator, *leaf, 0.0f, true);
}



// ---------------------------------------------------------------------------
// デバッグ表示
// ---------------------------------------------------------------------------

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
