/**
 * @file BossAISystem.cpp
 * @brief Boss AI システムの実装（マルチスレッド並列評価）
 */
#include "Game/Logics/AI/BehaviorTree/BossAISystem.h"
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeBuilder.h"
#include "Engine/Platform/Logger.h"
#include "Game/Core/SystemPriority.h" // LogicStage等の定義
#include "ECS/System/CCL_SystemRegistry.h"
#include "tracy/Tracy.hpp"

 // 規約に基づきcpp側でusing namespace
using namespace CCL::ECS;

BossAISystem::BossAISystem()
    : IfSystem("BossAISystem")
{
}

void BossAISystem::Initialize()
{
    // 初期化時にテスト用アセットを構築 (JSON等からのデータ駆動化が完了するまでの仮実装)
    CCL::AI::BehaviorTreeBuilder builder;
    builder
        .Selector()
        .Sequence()
        .Condition(2) // HP < 50%
        .Action(101)  // 突進攻撃
        .End()
        .Action(100)      // 通常近接攻撃
        .End();

    builder.Bake(m_bossSharedAsset);
    m_bossSharedAsset.id = 1;

    CCL_LOG_INFO(LogCategory::Game, "BossAISystem Initialized and BT Asset baked.");
}

void BossAISystem::Update(float dt)
{
    // Tracyプロファイラマクロ（規約に従いそのまま記述）
    ZoneScoped;

    // ★ 並列実行 (ForEachParallel) 
    // テンプレート引数とラムダの引数型を【完全に一致】させなければコンパイルエラーになる
    ForEachParallel([this](BehaviorTreeComponent& btComp, BossCommandComponent& cmdComp) {

        // 1. コマンドのリセット（前フレームの出力をクリア）
        cmdComp.requestMeleeAttack = false;
        cmdComp.requestCharge = false;
        cmdComp.requestGuard = false;

        // 2. 該当するアセットを参照してツリー評価
        if (btComp.assetId == m_bossSharedAsset.id) {
            EvaluateTreeIterative(0, btComp, cmdComp, m_bossSharedAsset);
        }
        });
}

CCL::AI::BTNodeState BossAISystem::EvaluateTreeIterative(
    CCL::AI::BTNodeID rootId,
    BehaviorTreeComponent& btComp,
    BossCommandComponent& cmdComp,
    const CCL::AI::BTAsset& asset)
{
    static constexpr int MAX_STACK_DEPTH = 16;
    CCL::AI::BTNodeID nodeStack[MAX_STACK_DEPTH];
    int stackTop = 0;

    nodeStack[stackTop++] = rootId;

    while (stackTop > 0) {
        CCL::AI::BTNodeID currentNodeId = nodeStack[--stackTop];
        const CCL::AI::BTNode& node = asset.nodes[currentNodeId];

        switch (node.type) {
        case CCL::AI::BTNodeType::Selector: {
            for (int i = 0; i < node.childCount; ++i) {
                nodeStack[stackTop++] = node.firstChildOffset + (node.childCount - 1 - i);
            }
            break;
        }
        case CCL::AI::BTNodeType::Sequence: {
            for (int i = 0; i < node.childCount; ++i) {
                nodeStack[stackTop++] = node.firstChildOffset + (node.childCount - 1 - i);
            }
            break;
        }
        case CCL::AI::BTNodeType::Condition: {
            if (!EvaluateCondition(node.actionOrConditionId, btComp)) {
                return CCL::AI::BTNodeState::Failure;
            }
            break;
        }
        case CCL::AI::BTNodeType::Action: {
            CCL::AI::BTNodeState actionState = ExecuteAction(node.actionOrConditionId, btComp, cmdComp);
            if (actionState == CCL::AI::BTNodeState::Running) {
                btComp.runningNodeId = currentNodeId;
                return CCL::AI::BTNodeState::Running;
            }
            break;
        }
        default:
            CCL_LOG_ERROR(LogCategory::Game, "Invalid BTNode Type detected!");
            break;
        }
    }
    return CCL::AI::BTNodeState::Success;
}

bool BossAISystem::EvaluateCondition(CCL::AI::ActionID condId, const BehaviorTreeComponent& btComp)
{
    switch (condId) {
    case 1: return btComp.distanceToTarget < 5.0f;
    case 2: return btComp.healthPercentage < 50.0f;
    default: return false;
    }
}

CCL::AI::BTNodeState BossAISystem::ExecuteAction(CCL::AI::ActionID actionId, BehaviorTreeComponent& btComp, BossCommandComponent& cmdComp)
{
    switch (actionId) {
    case 100:
        cmdComp.requestMeleeAttack = true;
        return CCL::AI::BTNodeState::Success;
    case 101:
        cmdComp.requestCharge = true;
        return CCL::AI::BTNodeState::Running;
    default:
        return CCL::AI::BTNodeState::Failure;
    }
}

// 規約に基づき末尾でシステム自動登録。
// 実行タイミングは「L02_Update (AI、移動、弾の更新など並列化の主戦場)」を指定。
// ※プロジェクト側に REGISTER_LOGIC_SYSTEM マクロが定義されている前提
REGISTER_LOGIC_SYSTEM(BossAISystem, Priority::LogicStage::L02_Update);