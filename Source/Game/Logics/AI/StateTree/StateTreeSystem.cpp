//#include "StateTreeSystem.h"
//#include "Engine/Platform/Core/Logger.h"
//#include "Game/Core/SystemPriority.h"
//
//using namespace CCL::ECS;
//
//void StateTreeSystem::Update(float rawDt)
//{
//    // ForEachParallelによる超並列評価。型の完全一致を保証すること。
//    ForEachParallel([this](StateTreeComponent& stComp, BossCommandComponent& cmdComp,
//        const TransformComponent& trans, const TimeState& time) {
//
//            float dt = time.localDt;
//
//            if (!stComp.sharedAsset || stComp.sharedAsset->states.empty()) return;
//
//            const StateTreeAsset& asset = *stComp.sharedAsset;
//            StateID currentState = stComp.currentStateId;
//
//            // 安全装置：無効なStateならRoot(0)へ
//            if (currentState >= asset.states.size()) {
//                currentState = 0;
//                stComp.currentStateId = 0;
//                ExecuteTasks(stComp, cmdComp, asset, currentState, StateTaskType::Enter, dt);
//            }
//
//            const StateTreeNode& node = asset.states[currentState];
//            bool transitioned = false;
//
//            // ========================================================
//            // 1. Transition Phase (遷移の評価)
//            // ========================================================
//            // 配列の局所性を活かし、現在のStateが持つ遷移条件だけを連続評価する
//            for (uint16_t i = 0; i < node.transitionCount; ++i) {
//                const auto& transition = asset.transitions[node.transitionStartIndex + i];
//
//                if (EvaluateCondition(transition.conditionId, stComp, trans)) {
//                    StateID targetState = transition.targetStateId;
//
//                    // LCA (最小共通祖先) の計算
//                    StateID lcaState = FindLCA(asset, currentState, targetState);
//
//                    // Exit Phase: 現在のStateからLCAの直前までExitタスクを実行
//                    StateID exitWalk = currentState;
//                    while (exitWalk != lcaState && exitWalk != InvalidStateID) {
//                        ExecuteTasks(stComp, cmdComp, asset, exitWalk, StateTaskType::Exit, dt);
//                        exitWalk = asset.states[exitWalk].parentId;
//                    }
//
//                    // Enter Phase: LCAの直下からターゲットStateまでEnterタスクを実行
//                    // (※実際にはターゲットからLCAまで遡ってスタックに積み、逆順でEnterを呼ぶ処理が必要)
//                    std::vector<StateID> enterStack; // 固定長配列(上限付き)に最適化するのが望ましい
//                    StateID enterWalk = targetState;
//                    while (enterWalk != lcaState && enterWalk != InvalidStateID) {
//                        enterStack.push_back(enterWalk);
//                        enterWalk = asset.states[enterWalk].parentId;
//                    }
//                    for (auto it = enterStack.rbegin(); it != enterStack.rend(); ++it) {
//                        ExecuteTasks(stComp, cmdComp, asset, *it, StateTaskType::Enter, dt);
//                    }
//
//                    // 状態の更新とフラグ立て
//                    stComp.currentStateId = targetState;
//                    transitioned = true;
//                    break; // 1フレームに1回の遷移のみ許可
//                }
//            }
//
//            // ========================================================
//            // 2. Tick Phase (タスクの実行)
//            // ========================================================
//            // 遷移しなかった場合のみ、現在のStateのTickタスクを実行する
//            if (!transitioned) {
//                // ※階層化されている場合、Rootから現在のStateまでの全Tickを回すか、
//                // 現在のStateのTickのみを回すかはゲームの要件による。
//                // ここでは最もシンプルな「現在のStateのTick」を回す。
//                ExecuteTasks(stComp, cmdComp, asset, stComp.currentStateId, StateTaskType::Tick, dt);
//            }
//
//        });
//}
//
//// ----------------------------------------------------------------------------
//// タスクの実行エンジン
//// ----------------------------------------------------------------------------
//void StateTreeSystem::ExecuteTasks(StateTreeComponent& stComp, BossCommandComponent& cmdComp,
//    const StateTreeAsset& asset, StateID stateId, StateTaskType execType, float dt)
//{
//    const StateTreeNode& node = asset.states[stateId];
//
//    // 現在のStateに紐づくタスク配列の範囲を走査
//    for (uint16_t i = 0; i < node.taskCount; ++i) {
//        uint16_t taskIndex = node.taskStartIndex + i;
//        const auto& task = asset.tasks[taskIndex];
//
//        // 実行タイミングが一致するタスクのみ処理
//        if (task.executionType == execType) {
//
//            // 例: ボス本体への命令(1000番台)
//            if (task.actionId >= 1000 && task.actionId < 2000) {
//                // Tickタスクなら毎フレーム命令を出し続ける
//                if (execType == StateTaskType::Tick) {
//                    cmdComp.currentActionId = task.actionId;
//                }
//                // Enterタスクなら、状態に入った瞬間に一発だけパラメータをセットする等
//            }
//            // ドローンへの命令(2000番台)
//            else if (task.actionId >= 2000 && task.actionId < 3000) {
//                cmdComp.requestFormation = static_cast<DroneFormationType>(task.actionId - 2000 + 1);
//            }
//        }
//    }
//}
//
//// ----------------------------------------------------------------------------
//// ツリーの階層を遡り、最小共通祖先(LCA)を見つける
//// ----------------------------------------------------------------------------
//StateID StateTreeSystem::FindLCA(const StateTreeAsset& asset, StateID source, StateID target)
//{
//    if (source == target) return source;
//
//    // 双方の深さを計算し、同じ深さまで親を辿り、その後同時に親を辿って一致する点を返す
//    // ※DOD観点：最大階層はたかが知れている（数階層）ため、ポインタジャンプによる
//    // キャッシュミスを気にせず while で回して問題ない。
//
//    auto getDepth = [&](StateID id) {
//        int depth = 0;
//        while (id != InvalidStateID && id != 0) {
//            id = asset.states[id].parentId;
//            depth++;
//        }
//        return depth;
//        };
//
//    int depthS = getDepth(source);
//    int depthT = getDepth(target);
//
//    StateID walkS = source;
//    StateID walkT = target;
//
//    while (depthS > depthT) { walkS = asset.states[walkS].parentId; depthS--; }
//    while (depthT > depthS) { walkT = asset.states[walkT].parentId; depthT--; }
//
//    while (walkS != walkT && walkS != InvalidStateID && walkT != InvalidStateID) {
//        walkS = asset.states[walkS].parentId;
//        walkT = asset.states[walkT].parentId;
//    }
//
//    return walkS != InvalidStateID ? walkS : 0; // 見つからなければRoot(0)を返す
//}
//
//bool StateTreeSystem::EvaluateCondition(ActionID condId, const StateTreeComponent& stComp, const TransformComponent& trans)
//{
//    // BTで実装していた BossAISystem::EvaluateCondition のロジックをそのまま移植可能
//    // stComp.blackboard の値を評価する
//    return false; // ※仮置き
//}
//
//REGISTER_LOGIC_SYSTEM(StateTreeSystem, Priority::LogicStage::L02_Update);