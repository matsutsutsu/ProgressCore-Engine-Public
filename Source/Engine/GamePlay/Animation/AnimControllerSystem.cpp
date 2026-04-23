#include "AnimControllerSystem.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"
#include <cmath> // std::abs用
#include "Engine/Graphics/Resource/ResourceManager.h"

#include "Engine/GamePlay/Animation/Data/AnimGraphSerializer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"


void AnimControllerSystem::Update(float dt)
{  
    auto& resourceManager = ResourceManager::Instance();

    // ForEachWithIDParallel は IfSystem から継承されているため直接呼び出し可能。
    // 引数の EntityID のみにエンジン側の型指定(CCL::ECS::)を行います。
    ForEachWithIDParallel([&resourceManager](CCL::ECS::EntityID id,
        AnimStateMachineComponent& fsm,
        AnimatorComponent& animator,
        const AnimParametersComponent& params)
        {
            // ========================================================================
            // グラフポインタが空だが、パスが設定されているならロードする (復元処理)
            // ========================================================================
            if (!fsm.graph && !fsm.graphPath.empty()) {
                if (AnimGraphSerializer::LoadFromJSON(fsm.internalGraph, fsm.graphPath, {})) {

                    // 各ステートの台本(AnimSequence)をResourceManagerから取得してポインタを繋ぐ
                    for (auto& state : fsm.internalGraph.states) {
                        // ※ノードエディタの「状態名」にJSONパスが入力されている前提です
                        state.sequence = ResourceManager::Instance().LoadAnimSequence(state.sequenceName.c_str());
                    }

                    fsm.graph = &fsm.internalGraph;
                    fsm.currentStateHash = fsm.internalGraph.entryStateHash; // 初期状態へ
                }
                else {
                    fsm.graphPath = ""; // ロード失敗時は無限ループを防ぐためクリア
                }
            }

            // グラフ(マスターデータ)が設定されていなければ処理しない
            if (!fsm.graph) return;

            // 1. 現在のステートを検索
            const AnimState* currentState = nullptr;
			// 全体のノードをfor文で回す　unoredered_mapでハッシュから直接アクセスする方法もあるが、ノード数が少ない前提でシンプルな線形探索を採用
            for (const auto& state : fsm.graph->states) {
				// 現在のステートを見つけたらループを抜ける
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

				// 条件リストを全てチェック（AND条件）
                for (const auto& cond : transition.conditions) {
					// 条件のパラメーター値をAnimParametersComponentから取得
					// 例: "Speed > 0.5" なら、paramHashは "Speed" のハッシュ値、thresholdは0.5fになる
                    float paramValue = params.GetFloat(cond.paramHash);

					// 条件演算子に応じて、パラメーター値と閾値を比較
					// cond.op　とは、AnimCondition構造体のメンバで、条件の演算子を表す列挙型（Equal, Greater, Less）です。
                    switch (cond.op) {
					case AnimConditionOp::Greater:  // 例: Speed > 0.5
                        if (!(paramValue > cond.threshold)) allConditionsMet = false;
                        break;
					case AnimConditionOp::Less:     // 例: Speed < 0.5
                        if (!(paramValue < cond.threshold)) allConditionsMet = false;
                        break;
					case AnimConditionOp::Equal:    // 例: IsGrounded == true (boolもfloatも0.0f/1.0fで表現できるので同じコードで処理可能)
                        // 浮動小数点数の厳密比較を避け、許容誤差(Epsilon)を設ける
                        if (std::abs(paramValue - cond.threshold) > 0.0001f) allConditionsMet = false;
                        break;
                    }

                    if (!allConditionsMet) break; // 1つでも満たさなければ次へ
                }

                // 3. 全ての条件を満たした場合、遷移を実行してループを抜ける
                if (allConditionsMet) {
					// 遷移先（矢印の先）のステートに切り替える
                    fsm.currentStateHash = transition.targetStateHash;

					// 遷移先のステートを検索して、対応するアニメーションを再生する
                    for (const auto& nextState : fsm.graph->states) {
						// 遷移先のステートを見つけたら、そのステートに対応するアニメーションを再生してループを抜ける
                        if (nextState.stateHash == transition.targetStateHash) {

                            if (nextState.sequence) {
                                // ========================================================
                                //  カーブのバインド処理
                                // ========================================================
                                const AnimationCurve* curvePtr = nullptr;
                                
                                // speedCurveName に文字列（パス）が設定されていればロードする
                                if (!nextState.speedCurveName.empty()) {
                                    // ResourceManager は一度ロードしたものはキャッシュから返すので、毎フレーム呼んでも安全です
                                    // ただし、理想を言えば AnimStateGraph をロードした段階で一括バインドする方がより高速です
                                    curvePtr = resourceManager.LoadAnimCurve(nextState.speedCurveName.c_str());
                                }

                                // 固定値ではなく、ノードに設定された値を使用する
                                // ★第4引数に curvePtr を渡す
                                animator.Play(nextState.sequence, nextState.isLoop, nextState.playbackSpeed, curvePtr);
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