#include "AnimControllerSystem.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"
#include <cmath> 
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/GamePlay/Animation/Data/AnimGraphSerializer.h"

void AnimControllerSystem::Update(float dt)
{
    auto& resourceManager = ResourceManager::Instance();

    ForEachWithIDParallel([&resourceManager](CCL::ECS::EntityID id,
        AnimStateMachineComponent& fsm,
        AnimatorComponent& animator,
        AnimParametersComponent& params)
        {
            // ========================================================================
            // 1. グラフのロードと初期化 (ポインタを使わず直接 internalGraph を操作する)
            // ========================================================================
            if (fsm.internalGraph.states.empty() && !fsm.graphPath.empty()) {
                if (AnimGraphSerializer::LoadFromJSON(fsm.internalGraph, fsm.graphPath, {})) {

                    for (auto& state : fsm.internalGraph.states) {
                        if (!state.sequenceFilePath.empty()) {
                            state.sequence = resourceManager.LoadAnimSequence(state.sequenceFilePath.c_str());
                        }
                    }

                    fsm.currentStateHash = fsm.internalGraph.entryStateHash; // 初期状態へ

                    // ★ 初期再生のブートストラップ
                    for (const auto& state : fsm.internalGraph.states) {
                        if (state.stateHash == fsm.currentStateHash) {
                            if (state.sequence) {
                                animator.Play(state.sequence, state.isLoop, state.playbackSpeed);
                            }
                            break;
                        }
                    }
                }
                else {
                    fsm.graphPath = ""; // ロード失敗時はクリア
                }
            }

            // グラフが空なら処理しない
            if (fsm.internalGraph.states.empty()) return;

            // ========================================================================
            // 2. 現在のステートを検索 (internalGraph を直接参照)
            // ========================================================================
            const AnimState* currentState = nullptr;
            for (const auto& state : fsm.internalGraph.states) {
                if (state.stateHash == fsm.currentStateHash) {
                    currentState = &state;
                    break;
                }
            }

            if (!currentState) {
                fsm.currentStateHash = fsm.internalGraph.entryStateHash;
                return;
            }

            // ========================================================================
            // 3. 遷移条件の評価
            // ========================================================================
            for (const auto& transition : currentState->transitions) {
                bool allConditionsMet = true;

                for (const auto& cond : transition.conditions) {
                    // トリガーの評価
                    if (cond.op == AnimConditionOp::Trigger) {
                        if (!params.HasTrigger(cond.paramHash)) {
                            allConditionsMet = false;
                        }
                        if (!allConditionsMet) break;
                        continue; // トリガーの場合はFloat/Boolの検索をスキップ
                    }

                    // ==========================================================
                    // アニメーション再生終了(Finished)の評価
                    // ==========================================================
                    if (cond.op == AnimConditionOp::Finished) {
                        // AnimatorComponent の isFinished フラグが立っていなければ遷移不可
                        if (!animator.isFinished) {
                            allConditionsMet = false;
                        }
                        if (!allConditionsMet) break;
                        continue; // Float/Boolの検索をスキップ
                    }

                    float paramValue = 0.0f;
                    bool isFound = false;

                    // Floatの検索
                    for (const auto& p : params.floats) {
                        if (p.hash == cond.paramHash) {
                            paramValue = p.value;
                            isFound = true;
                            break;
                        }
                    }

                    // Floatに無ければBoolを検索して変換
                    if (!isFound) {
                        for (const auto& p : params.bools) {
                            if (p.hash == cond.paramHash) {
                                paramValue = p.value ? 1.0f : 0.0f;
                                break;
                            }
                        }
                    }

                    switch (cond.op) {
                    case AnimConditionOp::Greater:
                        if (!(paramValue > cond.threshold)) allConditionsMet = false;
                        break;
                    case AnimConditionOp::Less:
                        if (!(paramValue < cond.threshold)) allConditionsMet = false;
                        break;
                    case AnimConditionOp::Equal:
                        if (std::abs(paramValue - cond.threshold) > 0.0001f) allConditionsMet = false;
                        break;
                    }

                    if (!allConditionsMet) break;
                }

                // ========================================================================
                // 4. 遷移の実行と再生命令
                // ========================================================================
                if (allConditionsMet) {
                    // 【超重要】この遷移で使用したトリガーを消費(削除)する
                    for (const auto& cond : transition.conditions) {
                        if (cond.op == AnimConditionOp::Trigger) {
                            params.ConsumeTrigger(cond.paramHash);
                        }
                    }                

                    fsm.currentStateHash = transition.targetStateHash;

                    // 遷移先も internalGraph から探す
                    for (const auto& nextState : fsm.internalGraph.states) {
                        if (nextState.stateHash == transition.targetStateHash) {

                            if (nextState.sequence) {
                                const AnimationCurve* curvePtr = nullptr;
                                if (!nextState.speedCurveName.empty()) {
                                    curvePtr = resourceManager.LoadAnimCurve(nextState.speedCurveName.c_str());
                                }
                                animator.Play(nextState.sequence, nextState.isLoop, nextState.playbackSpeed, curvePtr);
                            }
                            break;
                        }
                    }
                    break;
                }
            }
        });
}


REGISTER_LOGIC_SYSTEM(AnimControllerSystem, Priority::LogicStage::L02_PostUpdate);