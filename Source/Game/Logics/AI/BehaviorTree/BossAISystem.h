/**
 * @file BossAISystem.h
 * @brief Boss AIの思考を評価し、コマンドを出力するシステム
 * @note 名前空間のネストを防ぐため、グローバルに定義
 */
#pragma once
#include "ECS/System/CCL_System.h"
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeComponents.h"
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeData.h"

 // IfSystemのテンプレート引数で要求コンポーネントを厳密に定義
class BossAISystem : public CCL::ECS::IfSystem<
    BossAISystem,
    CCL::ECS::Write<BehaviorTreeComponent>,
    CCL::ECS::Write<BossCommandComponent>
>
{
public:
    BossAISystem();
    virtual ~BossAISystem() override = default;

    virtual void Initialize() override;
    virtual void Update(float dt) override;

private:
    CCL::AI::BTNodeState EvaluateTreeIterative(
        CCL::AI::BTNodeID rootId,
        BehaviorTreeComponent& btComp,
        BossCommandComponent& cmdComp,
        const CCL::AI::BTAsset& asset
    );
    bool EvaluateCondition(CCL::AI::ActionID condId, const BehaviorTreeComponent& btComp);
    CCL::AI::BTNodeState ExecuteAction(CCL::AI::ActionID actionId, BehaviorTreeComponent& btComp, BossCommandComponent& cmdComp);

    // （仮）メモリ上に保持するボスAIアセット。
    // ※本来は _world->GetResource<ResourceManager> 等で取得すべき共有データです
    CCL::AI::BTAsset m_bossSharedAsset;
};