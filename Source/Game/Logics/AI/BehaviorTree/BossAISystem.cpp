#include "Game/Logics/AI/BehaviorTree/BossAISystem.h"
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeBuilder.h"
#include "Engine/Platform/Logger.h"
#include "Game/Core/SystemPriority.h" // LogicStage等の定義
#include "ECS/System/CCL_SystemRegistry.h"
#include "tracy/Tracy.hpp"

/**
 * @file BossAISystem.cpp
 * @brief ボスAIシステムの実装
 * * 役割：
 * ForEachParallel を用いた超並列AI評価。
 * * なぜこの設計か（Why）：
 * 1. 仮想関数を排除した switch 分岐により、CPUパイプラインを停止させない。
 * 2. 再帰呼び出しを排除し、スタックを自前管理（固定長配列）することで、
 * 関数呼び出しのオーバーヘッドをゼロにし、メモリ安全性と速度を両立している。
 * 3. 60fps維持のため、AIは「コマンドを発行するだけ」に留め、
 * 物理演算やアニメーションとの依存を最小限に抑えている。
 */

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
    // ★ 並列実行 (ForEachParallel) 
    // テンプレート引数とラムダの引数型を【完全に一致】させなければコンパイルエラーになる
    ForEachParallel([this](BehaviorTreeComponent& btComp, BossCommandComponent& cmdComp) {

        // 1. コマンドのリセット（前フレームの出力をクリア）
        cmdComp.requestMeleeAttack = false;
        cmdComp.requestCharge = false;
        //cmdComp.requestGuard = false;

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
    // 固定長配列による「自前スタック」。newによるヒープ確保を避け、最速を叩き出す。
    // 最大深度を16に制限。複雑なAIでも深度16を超えることは稀。
    static constexpr int MAX_STACK_DEPTH = 16;

    // ヒープ(new)を使わず、スタックメモリ上に直接配列を確保（爆速）
    CCL::AI::BTNodeID nodeStack[MAX_STACK_DEPTH];
    int stackTop = 0;

    // 最初にルートノード（0番）をスタックに積む
    nodeStack[stackTop++] = rootId; // ルートを積む

    // スタックに「次にやる仕事」が残っている限りループを回す
    while (stackTop > 0) {
        // スタックの一番上からノードIDを取り出し、stackTopを減らす (Pop操作)
        CCL::AI::BTNodeID currentNodeId = nodeStack[--stackTop];

        // 配列から実際のノードデータを取得。キャッシュに優しく連続アクセスされる。
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
    case 1: return btComp.blackboard.distanceToTarget < 5.0f;
    case 2: return btComp.blackboard.healthPercentage < 50.0f;
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