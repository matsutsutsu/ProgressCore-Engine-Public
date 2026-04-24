#include "Game/Logics/AI/BehaviorTree/BossAISystem.h"
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeBuilder.h"
#include "Engine/Platform/Logger.h"
#include "Game/Core/SystemPriority.h" // LogicStage等の定義
#include "ECS/System/CCL_SystemRegistry.h"


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

BossAISystem::BossAISystem() : IfSystem("BossAISystem") {}

// ==========================================================
// ボスAI専用のID定義（マジックナンバーの排除）
// ==========================================================
namespace BossAI_ID {
    // 状態遷移のための条件ID
    inline constexpr CCL::AI::ActionID Cond_PhaseMove = 1;
    inline constexpr CCL::AI::ActionID Cond_PhaseMelee = 2;

    // 行動の出力ID
    inline constexpr CCL::AI::ActionID Act_MeleeAttack = 100;
    inline constexpr CCL::AI::ActionID Act_ChargeAttack = 101;
    inline constexpr CCL::AI::ActionID Act_Move = 102; // ★移動用アクション追加
}

void BossAISystem::Initialize()
{
    // 初期化時にテスト用アセットを構築 (JSON等からのデータ駆動化が完了するまでの仮実装)
    CCL::AI::BehaviorTreeBuilder builder;
    builder
        .Selector()
            .Sequence() // パターン1：0〜3秒なら移動
                .Condition(BossAI_ID::Cond_PhaseMove)
                .Action(BossAI_ID::Act_Move)
            .End()
            .Sequence() // パターン2：3〜6秒なら近接攻撃
                .Condition(BossAI_ID::Cond_PhaseMelee)
                .Action(BossAI_ID::Act_MeleeAttack)
            .End()
            // パターン3：上記以外（6〜9秒）なら突進
            .Action(BossAI_ID::Act_ChargeAttack) 
        .End();

    builder.Bake(m_bossSharedAsset);
    m_bossSharedAsset.id = 1;

    CCL_LOG_INFO(LogCategory::Game, "BossAISystem Initialized and BT Asset baked.");
}

void BossAISystem::Update(float dt)
{
    // ★ 並列実行 (ForEachParallel) 
    // テンプレート引数とラムダの引数型を【完全に一致】させなければコンパイルエラーになる
    ForEachParallel([this,dt](BehaviorTreeComponent& btComp, BossCommandComponent& cmdComp) {

        // 前フレームのフラグを全てリセット
        cmdComp.requestMeleeAttack = false;
        cmdComp.requestCharge = false;
        cmdComp.requestGuard = false;
        cmdComp.requestMove = false;

        // ★テストギミック：距離変数を「経過タイマー」として流用（9秒でループ）
        btComp.blackboard.distanceToTarget += dt;
        if (btComp.blackboard.distanceToTarget > 15.0f) {
            btComp.blackboard.distanceToTarget = 0.0f;
        }

        // 2. 該当するアセットを参照してツリー評価
        if (btComp.assetId == m_bossSharedAsset.id) {
            // ★ここを EvaluateTreeRecursive に変更
            EvaluateTreeRecursive(0, btComp, cmdComp, m_bossSharedAsset);
        }
        });
}

CCL::AI::BTNodeState BossAISystem::EvaluateTreeRecursive(
    CCL::AI::BTNodeID nodeId,
    BehaviorTreeComponent& btComp,
    BossCommandComponent& cmdComp,
    const CCL::AI::BTAsset& asset)
{
    const CCL::AI::BTNode& node = asset.nodes[nodeId];

    switch (node.type) {
    case CCL::AI::BTNodeType::Selector: {
        // 左から順番に試し、どれか一つでも「成功(Success)」か「実行中(Running)」なら、そこで終了
        for (int i = 0; i < node.childCount; ++i) {
            auto state = EvaluateTreeRecursive(node.firstChildOffset + i, btComp, cmdComp, asset);
            if (state != CCL::AI::BTNodeState::Failure) {
                return state;
            }
        }
        return CCL::AI::BTNodeState::Failure; // 全て失敗したらFailure
    }
    case CCL::AI::BTNodeType::Sequence: {
        // 左から順番に試し、途中で「失敗(Failure)」か「実行中(Running)」が出たら、そこで中断
        for (int i = 0; i < node.childCount; ++i) {
            auto state = EvaluateTreeRecursive(node.firstChildOffset + i, btComp, cmdComp, asset);
            if (state != CCL::AI::BTNodeState::Success) {
                return state;
            }
        }
        return CCL::AI::BTNodeState::Success; // 全て成功したらSuccess
    }
    case CCL::AI::BTNodeType::Condition: {
        // 条件を満たしていればSuccess、ダメならFailure
        return EvaluateCondition(node.actionOrConditionId, btComp)
            ? CCL::AI::BTNodeState::Success
            : CCL::AI::BTNodeState::Failure;
    }
    case CCL::AI::BTNodeType::Action: {
        // 実際のアクションを実行し、状態を返す
        CCL::AI::BTNodeState actionState = ExecuteAction(node.actionOrConditionId, btComp, cmdComp);
        if (actionState == CCL::AI::BTNodeState::Running) {
            btComp.runningNodeId = nodeId;
        }
        return actionState;
    }
    default:
        return CCL::AI::BTNodeState::Failure;
    }
}

// 条件を評価する関数。条件IDに応じて、Blackboardの情報を参照して真偽を返す。
bool BossAISystem::EvaluateCondition(CCL::AI::ActionID condId, const BehaviorTreeComponent& btComp)
{
    switch (condId) {
    case BossAI_ID::Cond_PhaseMove:
        return btComp.blackboard.distanceToTarget < 5.0f; // 0〜5秒の間は成功
    case BossAI_ID::Cond_PhaseMelee:
        return btComp.blackboard.distanceToTarget >= 5.0f && btComp.blackboard.distanceToTarget < 10.0f; // 5〜10秒の間は成功
    default: return false;
    }
}

// 行動を評価する関数。行動IDに応じて、cmdCompに行動要求を書き込む。
CCL::AI::BTNodeState BossAISystem::ExecuteAction(CCL::AI::ActionID actionId, BehaviorTreeComponent& btComp, BossCommandComponent& cmdComp)
{
    // ---------------------------------------------------------
    // ★ 状態遷移ロギング (エッジ検知)
    // ---------------------------------------------------------
    if (btComp.lastLogActionId != actionId) {
        switch (actionId) {
        case BossAI_ID::Act_Move:
            CCL_LOG_INFO(LogCategory::Game, "[BossAI] State Changed -> MOVE (ウロウロ中)");
            break;
        case BossAI_ID::Act_MeleeAttack:
            // 攻撃実行は目立たせるために SUCCESS (緑色) を使用
            CCL_LOG_SUCCESS(LogCategory::Game, "[BossAI] State Changed -> MELEE ATTACK (近接攻撃！)");
            break;
        case BossAI_ID::Act_ChargeAttack:
            // 突進などの危険行動は WARN (黄色) を使用
            CCL_LOG_WARN(LogCategory::Game, "[BossAI] State Changed -> CHARGE ATTACK (突進開始！)");
            break;
        }
        // 記憶を更新（これ以降、別の行動になるまでログは沈黙する）
        btComp.lastLogActionId = actionId;
    }

    switch (actionId) {
    case BossAI_ID::Act_Move:
        cmdComp.requestMove = true;
        return CCL::AI::BTNodeState::Running;
    case BossAI_ID::Act_MeleeAttack:
        cmdComp.requestMeleeAttack = true;
        return CCL::AI::BTNodeState::Success;
    case BossAI_ID::Act_ChargeAttack:
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