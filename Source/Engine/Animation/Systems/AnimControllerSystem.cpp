#include "AnimControllerSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif
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

        // ★ DODの極み: 窓のマスクと、遷移が要求するマスクのビットが「1つでも」被っていればOK
        if ((ev.cancelMask & requiredMask) == 0)
            continue;

        if (animator.currentTimer >= ev.startTime &&
            animator.currentTimer <= ev.endTime)
            return true;
    }
    return false;
}

/// @brief ステートマシン評価。ロード/束縛（シリアル）と評価（並列）を2フェーズに分離する。
/// @details フェーズ1で全ポインタ(sequence/speedCurve)を束縛し、フェーズ2の並列評価から
///          ResourceManager（共有シングルトン）・ファイルI/Oを完全に排除してレースを防ぐ。
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

        // 全ステートの sequence / speedCurve ポインタをここで束縛する。
        // これにより並列評価フェーズ(2)から ResourceManager を呼ぶ必要がなくなる。
        for (auto &state : fsm.internalGraph.states)
        {
            if (!state.sequenceFilePath.empty())
                state.sequence = resourceManager.LoadAnimSequence(state.sequenceFilePath.c_str());

            if (!state.speedCurveName.empty())
                state.speedCurve = resourceManager.LoadAnimCurve(state.speedCurveName.c_str());

             if (!state.rootMotionCurveName.empty())
                state.rootMotionCurve = resourceManager.LoadAnimCurve(state.rootMotionCurveName.c_str());

        }

        fsm.currentStateHash = fsm.internalGraph.entryStateHash; // 初期状態へ

        // 初期再生のブートストラップ（最初の再生なのでブレンドは0）
        for (const auto &state : fsm.internalGraph.states)
        {
            if (state.stateHash == fsm.currentStateHash)
            {
                if (state.sequence)
                {
                    animator.Play(state.sequence,
                        {.blendTime                 = 0.0f,
                            .loop                   = state.isLoop,
                            .speed                  = state.playbackSpeed,
                            .curve                  = state.speedCurve,
                            .rootMotionCurve        = state.rootMotionCurve,
                            .lockVerticalRootMotion = state.lockVerticalRootMotion
                        });
                }
                break;
            }
        }
    });

    // ログフラグをローカルにコピーし、ラムダ式にキャプチャさせてスレッドセーフにする
    bool enableLog = false;
#ifdef ENABLE_EDITOR
    enableLog = _enableTransitionLog;
#endif

    // =====================================================================
    // フェーズ2【並列】純粋なステートマシン評価
    //   ResourceManager / ファイルI/O は一切呼ばない（フェーズ1で束縛済み）。
    // =====================================================================
    ForEachWithIDParallel([enableLog](CCL::ECS::EntityID id,
                              AnimStateMachineComponent &fsm,
                              AnimatorComponent         &animator,
                              AnimParametersComponent   &params,
                              const TimeStateComponent  &time) { // ★ TimeStateComponent を追加
        if (animator.isEditorOverride) return;

        // ★★★ コアロジック: ステート評価の前に、先行入力バッファの寿命を削ぎ落とす ★★★
        // time.localDt を使うことで、ヒットストップやスローモーション中も完璧に同期する
        params.TickTriggers(time.localDt);

        if (fsm.internalGraph.states.empty()) return;

        // ====================================================================
        // 1. 現在のステートを検索
        // ====================================================================
        const AnimState *currentState = nullptr;
        for (const auto &state : fsm.internalGraph.states)
        {
            if (state.stateHash == fsm.currentStateHash)
            {
                currentState = &state;
                break;
            }
        }

        if (!currentState)
        {
            fsm.currentStateHash = fsm.internalGraph.entryStateHash;
            return;
        }

        // ====================================================================
        // 2. 遷移条件の評価 (評価ロジックの共通化)
        // ====================================================================
        const AnimTransition *triggeredTransition = nullptr;

        // 与えられた遷移リストを順に評価するラムダ式
        auto EvaluateTransitions = [&](const std::vector<AnimTransition> &transitions, bool isAnyState) -> bool {
            for (const auto &transition : transitions)
            {
                // ★ DOD/FSMの定石: Any Stateからの遷移の場合、自分自身への遷移は無視する（無限ループ防止）
                if (isAnyState && transition.targetStateHash == fsm.currentStateHash)
                    continue;

               // ★ ビットマスク版の判定関数を呼び出す
                if (!IsInCancelWindow(animator, transition.requiredCancelMask)) continue;

                bool allConditionsMet = true;

                for (const auto &cond : transition.conditions)
                {
                    // トリガーの評価
                    if (cond.op == AnimConditionOp::Trigger)
                    {
                        if (!params.HasTrigger(cond.paramHash))
                            allConditionsMet = false;
                        if (!allConditionsMet)
                            break;
                        continue; // トリガーは Float/Bool 検索をスキップ
                    }

                    // アニメーション再生終了(Finished)の評価
                    if (cond.op == AnimConditionOp::Finished)
                    {
                        if (!animator.isFinished)
                            allConditionsMet = false;
                        if (!allConditionsMet)
                            break;
                        continue;
                    }

                    // Float→Bool の順で検索
                    float paramValue = 0.0f;
                    params.TryGetNumeric(cond.paramHash, paramValue);

                    switch (cond.op)
                    {
                    case AnimConditionOp::Greater:
                        if (!(paramValue > cond.threshold))
                            allConditionsMet = false;
                        break;
                    case AnimConditionOp::Less:
                        if (!(paramValue < cond.threshold))
                            allConditionsMet = false;
                        break;
                    case AnimConditionOp::Equal:
                        if (std::abs(paramValue - cond.threshold) > 0.0001f)
                            allConditionsMet = false;
                        break;
                    default:
                        break;
                    }

                    if (!allConditionsMet)
                        break;
                }

                // 全条件を満たしたら、この遷移を採用してループを抜ける
                if (allConditionsMet)
                {
                    triggeredTransition = &transition;
                    return true;
                }
            }
            return false; // 条件を満たす遷移が無かった
        };

        // ★ 優先度1: まず Any State の遷移リストをチェックする (割り込み判定)
        if (!EvaluateTransitions(fsm.internalGraph.anyStateTransitions, true))
        {
            // ★ 優先度2: 割り込みが無ければ、現在のステートの遷移リストをチェックする
            EvaluateTransitions(currentState->transitions, false);
        }

        // ================================================================
        // 3. 遷移の実行と再生命令
        // ================================================================
        if (triggeredTransition)
        {
            // この遷移で使用したトリガーを消費(削除)する
            for (const auto &cond : triggeredTransition->conditions)
            {
                if (cond.op == AnimConditionOp::Trigger)
                {
                    params.ConsumeTrigger(cond.paramHash);

#ifdef ENABLE_EDITOR
                    if (enableLog)
                    {
                        CCL_LOG_INFO(LogCategory::Game, "[AnimController] Consumed Trigger: '%s' (0x%08X)",
                            cond.paramName.c_str(), cond.paramHash);
                    }
#endif
                }
            }

            std::string oldStateName = currentState->sequenceName;
            fsm.currentStateHash     = triggeredTransition->targetStateHash;

            // 遷移先を internalGraph から探す
            for (const auto &nextState : fsm.internalGraph.states)
            {
                if (nextState.stateHash == triggeredTransition->targetStateHash)
                {
#ifdef ENABLE_EDITOR
                    if (enableLog)
                    {
                        CCL_LOG_SUCCESS(LogCategory::Game, "[AnimController] Entity %d Transitioned: [%s] -> [%s]",
                            (int)id, oldStateName.c_str(), nextState.sequenceName.c_str());
                    }
#endif

                    if (nextState.sequence)
                    {
                        animator.Play(nextState.sequence,
                            {.blendTime                 = triggeredTransition->blendDuration,
                                .loop                   = nextState.isLoop,
                                .speed                  = nextState.playbackSpeed,
                                .curve                  = nextState.speedCurve,
                                .rootMotionCurve        = nextState.rootMotionCurve,
                                .lockVerticalRootMotion = nextState.lockVerticalRootMotion
                            });
                    }
                    break;
                }
            }
        }
    });
}


// システムのインスペクタ表示
#ifdef ENABLE_EDITOR
void AnimControllerSystem::OnGui()
{
    ImGui::Text("Animation Controller Settings");
    ImGui::Separator();

    ImGui::Checkbox("Enable Transition Logs", &_enableTransitionLog);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("チェックを入れると、ステートが切り替わった瞬間にコンソールへログを出力します。\n高速遷移によるログ汚染を防ぐため、デバッグ時のみONにしてください。");
    }
}
#else
void AnimControllerSystem::OnGui() {}
#endif


REGISTER_LOGIC_SYSTEM(AnimControllerSystem, Priority::LogicStage::L02_Update);