//#pragma once
//#include "Engine/ECS/System/CCL_System.h"
//#include "Game/Logics/AI/StateTree/StateTreeComponents.h"
//#include "Game/Logics/AI/StateTree/StateTreeData.h"
//#include "Game/Logics/AI/BehaviorTree/BossActionComponent.h"
//#include "Engine/Transform/Components/TransformComponent.h"
//#include "Engine/Core/Time/TimeState.h"
//
//// namespaceの全体囲みは禁止規約に従い除外
//
//class StateTreeSystem : public CCL::ECS::IfSystem<
//    StateTreeSystem,
//    CCL::ECS::Write<StateTreeComponent>,
//    CCL::ECS::Write<BossCommandComponent>,
//    CCL::ECS::Read<TransformComponent>,
//    CCL::ECS::Read<TimeState>
//>
//{
//public:
//    StateTreeSystem() : IfSystem("StateTreeSystem") {}
//    virtual ~StateTreeSystem() override = default;
//
//    virtual void Update(float dt) override;
//
//private:
//    // 遷移条件の評価（BTのConditionと同等）
//    bool EvaluateCondition(ActionID condId, const StateTreeComponent& stComp, const TransformComponent& trans);
//
//    // タスクの実行（Enter / Tick / Exit）
//    void ExecuteTasks(StateTreeComponent& stComp, BossCommandComponent& cmdComp,
//        const StateTreeAsset& asset, StateID stateId, StateTaskType execType, float dt);
//
//    // 最小共通祖先(LCA)を見つける関数
//    StateID FindLCA(const StateTreeAsset& asset, StateID source, StateID target);
//};