#include "AnimControllerSystem.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"
#include <cmath> // std::abs用

void AnimControllerSystem::Update(float dt)
{
    // ForEachWithIDParallel は IfSystem から継承されているため直接呼び出し可能。
    // 引数の EntityID のみにエンジン側の型指定(CCL::ECS::)を行います。
    ForEachWithIDParallel([](CCL::ECS::EntityID id,
        AnimStateMachineComponent& fsm,
        AnimatorComponent& animator,
        const AnimParametersComponent& params)
        {
            // グラフ(マスターデータ)が設定されていなければ処理しない
            if (!fsm.graph) return;

            // 1. 現在のステートを検索
            const AnimState* currentState = nullptr;
            for (const auto& state : fsm.graph->states) {
                if (state.stateHash == fsm.currentStateHash) {
                    currentState = &state;
                    break;
                }
            }

            // 現在のステートが見つからない場合は、エントリーステートに強制設定
            if (!currentState) {
                fsm.currentStateHash = fsm.graph->entryStateHash;
                return;
            }

            // 2. 遷移条件（現在ステートから伸びる矢印）を評価
            for (const auto& transition : currentState->transitions) {
                bool allConditionsMet = true;

                for (const auto& cond : transition.conditions) {
                    float paramValue = params.GetFloat(cond.paramHash);

                    switch (cond.op) {
                        // AnimConditionOp が特定の名前空間にある場合は適宜修飾してください
                    case AnimConditionOp::Greater:
                        if (!(paramValue > cond.threshold)) allConditionsMet = false;
                        break;
                    case AnimConditionOp::Less:
                        if (!(paramValue < cond.threshold)) allConditionsMet = false;
                        break;
                    case AnimConditionOp::Equal:
                        // 浮動小数点数の厳密比較を避け、許容誤差(Epsilon)を設ける
                        if (std::abs(paramValue - cond.threshold) > 0.0001f) allConditionsMet = false;
                        break;
                    }

                    if (!allConditionsMet) break; // 1つでも満たさなければ次へ
                }

                // 3. 全ての条件を満たした場合、遷移を実行してループを抜ける
                if (allConditionsMet) {
                    fsm.currentStateHash = transition.targetStateHash;

                    for (const auto& nextState : fsm.graph->states) {
                        if (nextState.stateHash == transition.targetStateHash) {

                            if (nextState.sequence) {
                                animator.Play(nextState.sequence, true, 1.0f);
                            }
                            break;
                        }
                    }
                    break; // 無限ループ防止のため1フレーム1遷移
                }
            }
        });
}

// 優先度設定：グローバルスコープでマクロを実行
REGISTER_LOGIC_SYSTEM(AnimControllerSystem, Priority::LogicStage::L02_Update);