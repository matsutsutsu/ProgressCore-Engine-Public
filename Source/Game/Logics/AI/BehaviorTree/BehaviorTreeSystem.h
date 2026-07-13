/// @file  BehaviorTreeSystem.h
/// @brief 汎用の BehaviorTree 評価エンジン。BossAISystem から抽出した。
///
/// ★役割★
///   BehaviorTreeComponent を持つ「どんなエンティティ」でも評価する。
///   ボス固有の概念（BossCommand/BossAction）を一切知らない。
///   Action/Condition は BTActionRegistry から引く（switch廃止）。
///
/// ★ボス固有処理はどこへ行ったか★
///   「移動してほしい」「この陣形にしてほしい」等の出力は、
///   各 IBTAction（BTActions_Boss.h）が world から BossCommandComponent を
///   自分で取得して書き込む。エンジンは関与しない。
///
/// ★フィルタ★
///   Write<BehaviorTreeComponent>, Read<TransformComponent>, Read<TimeStateComponent>
///   のみ。HealthComponent や BossActionComponent への依存を外したので、
///   それらを持たないエンティティ（将来のドローン等）でも動く。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"

class BehaviorTreeSystem : public CCL::ECS::IfSystem<
                               BehaviorTreeSystem,
                               CCL::ECS::Write<BehaviorTreeComponent>,
                               CCL::ECS::Read<TransformComponent>,
                               CCL::ECS::Read<TimeStateComponent>>
{
  public:
    BehaviorTreeSystem();
    virtual ~BehaviorTreeSystem() override = default;

    void Initialize() override; // BTActions/BTConditions の登録
    void Update(float dt) override;

    void OnGui() override;

  private:
    /// @brief 木を再帰的に評価する汎用エンジン。
    ///        Action/Condition は registry から引いて実行する。
    BTNodeState EvaluateTreeRecursive(
        BTNodeID                  nodeId,
        BehaviorTreeComponent    &btComp,
        const TransformComponent &myTrans,
        const BTAsset            &asset,
        CCL::ECS::EntityID        entity,
        float                     dt);
};