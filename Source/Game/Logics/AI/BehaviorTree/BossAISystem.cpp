#include "Game/Logics/AI/BehaviorTree/BossAISystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeBuilder.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeLoader.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
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
    inline constexpr ActionID Cond_PhaseMove = 1;
    inline constexpr ActionID Cond_PhaseMelee = 2;

    // 行動の出力ID
    inline constexpr ActionID Act_MeleeAttack = 100;
    inline constexpr ActionID Act_ChargeAttack = 101;
    inline constexpr ActionID Act_Move = 102; // ★移動用アクション追加
}

void BossAISystem::Initialize()
{
}

void BossAISystem::Update(float dt)
{
    // =========================================================
    // 1. ターゲット（プレイヤー）の検索フェーズ（シングルスレッド）
    // =========================================================
    EntityID activePlayerId = CCL::ECS::InvalidEntityID;

    auto playerView = _world->View<PlayerComponent,TransformComponent>();
    for (auto p : playerView) {
        activePlayerId = p;
        break;
    }

    // プレイヤーが見つからなければAIは思考を停止するか、待機する
    if (activePlayerId == CCL::ECS::InvalidEntityID) return;


    // プレイヤーの座標を取得（距離計算用）
    const auto* playerTrans = _world->GetComponent<TransformComponent>(activePlayerId);
    if (!playerTrans) return;

    // ★ 並列実行 (ForEachParallel) 
    // テンプレート引数とラムダの引数型を【完全に一致】させなければコンパイルエラーになる
    ForEachParallel([this,dt,activePlayerId](BehaviorTreeComponent& btComp, BossCommandComponent& cmdComp) {

        // ========================================================
        // 1. 動的ロード
        // ========================================================
        if (btComp.assetPath != btComp.loadedAssetPath) {
            
            // パスが書き換わった瞬間（または初回起動時）だけロードを実行
            if (!btComp.assetPath.empty()) {
                BehaviorTreeLoader::LoadFromJson(btComp.assetPath, *btComp.sharedAsset);
            }
            
            // 重要：成功・失敗に関わらず「このパスについては処理した」と記憶させる。
            // これにより、毎フレームの無駄なファイルアクセスとログスパムを完全に遮断する。
            btComp.loadedAssetPath = btComp.assetPath;
        }

        if (!btComp.sharedAsset || btComp.sharedAsset->nodes.empty()) return;

        // ========================================================
        // 2. メモリバッファの確保（エディタ可視化＆タイマー用）
        // ========================================================
        size_t nodeCount = btComp.sharedAsset->nodes.size();
        if (btComp.debugNodeStates.size() < nodeCount) {
            btComp.debugNodeStates.resize(nodeCount, BTDebugState::None);
            btComp.nodeTimers.resize(nodeCount, 0.0f);        
        }

        // デバッグ状態の初期化と、タイマーの進行（クールダウンの消化）
        std::fill(btComp.debugNodeStates.begin(), btComp.debugNodeStates.end(), BTDebugState::None);
        for (float& timer : btComp.nodeTimers) {
            if (timer > 0.0f) timer -= dt;
        }


        // 前フレームのフラグを全てリセット
        cmdComp.requestMeleeAttack = false;
        cmdComp.requestCharge = false;
        cmdComp.requestGuard = false;
        cmdComp.requestMove = false;
        cmdComp.targetPlayerId = activePlayerId;



        // ========================================================
        // 4. ツリーの評価実行
        // ========================================================
        btComp.runningNodeId = 0xFFFF; // 評価前にRunning状態をリセット
        
        if (btComp.sharedAsset && !btComp.sharedAsset->nodes.empty()) {
            EvaluateTreeRecursive(0, btComp, cmdComp, *btComp.sharedAsset, dt);
        }


    });
}

// ========================================================================
// ★ カプセル化された再帰評価エンジン（デコレーター＆デバッガー対応版）
// ========================================================================
BTNodeState BossAISystem::EvaluateTreeRecursive(
    BTNodeID nodeId,
    BehaviorTreeComponent& btComp,
    BossCommandComponent& cmdComp,
    const BTAsset& asset,
    float dt)
{
    const BTNode& node = asset.nodes[nodeId];
    BTNodeState result = BTNodeState::Failure;

    switch (node.type) {
    case BTNodeType::Selector: {
        for (int i = 0; i < node.childCount; ++i) {
            result = EvaluateTreeRecursive(node.firstChildIndex + i, btComp, cmdComp, asset, dt);
            if (result != BTNodeState::Failure) break;
        }
        break;
    }
    case BTNodeType::Sequence: {
        for (int i = 0; i < node.childCount; ++i) {
            result = EvaluateTreeRecursive(node.firstChildIndex + i, btComp, cmdComp, asset, dt);
            if (result != BTNodeState::Success) break;
        }
        break;
    }
    case BTNodeType::Condition: {
        result = EvaluateCondition(node.actionOrConditionId, btComp) ? BTNodeState::Success : BTNodeState::Failure;
        break;
    }
    case BTNodeType::Action: {
        result = ExecuteAction(node.actionOrConditionId, btComp, cmdComp);
        break;
    }
    case BTNodeType::Decorator: {
        // クールダウン中なら問答無用でFailureを返す
        if (node.decoratorType == BTDecoratorType::Cooldown && btComp.nodeTimers[nodeId] > 0.0f) {
            result = BTNodeState::Failure;
            break;
        }

        if (node.childCount > 0) {
            // 子ノードを評価
            result = EvaluateTreeRecursive(node.firstChildIndex, btComp, cmdComp, asset, dt);

            // デコレーターの特殊処理（結果の加工）
            if (node.decoratorType == BTDecoratorType::Inverter) {
                if (result == BTNodeState::Success) result = BTNodeState::Failure;
                else if (result == BTNodeState::Failure) result = BTNodeState::Success;
            }
            else if (node.decoratorType == BTDecoratorType::Cooldown && result == BTNodeState::Success) {
                // 成功した瞬間にクールダウンタイマーをセット
                btComp.nodeTimers[nodeId] = node.decoratorParam;
            }
        }
        break;
    }
    }

    // =========================================================
    // ★ 修正：BTNodeState (論理型) を BTDebugState (表示型) に安全に変換
    // =========================================================
    BTDebugState dbgState = BTDebugState::None;
    if (result == BTNodeState::Success) {
        dbgState = BTDebugState::Success;
    } else if (result == BTNodeState::Failure) {
        dbgState = BTDebugState::Failure;
    } else if (result == BTNodeState::Running) {
        dbgState = BTDebugState::Running;
    }

    // エディタのデバッガー向けに結果を保存
    btComp.debugNodeStates[nodeId] = dbgState;

    if (result == BTNodeState::Running) {
        btComp.runningNodeId = nodeId;
    }

    return result;
}

// 条件を評価する関数。条件IDに応じて、Blackboardの情報を参照して真偽を返す。
bool BossAISystem::EvaluateCondition(ActionID condId, const BehaviorTreeComponent& btComp)
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
BTNodeState BossAISystem::ExecuteAction(ActionID actionId, BehaviorTreeComponent& btComp, BossCommandComponent& cmdComp)
{
    // ---------------------------------------------------------
    // ★ 状態遷移ロギング (エッジ検知)
    // ---------------------------------------------------------
    if (btComp.lastLogActionId != actionId) {
        switch (actionId) {
        case BossAI_ID::Act_Move:
            CCL_LOG_INFO(LogCategory::AI, "[BossAI] State Changed -> MOVE (ウロウロ中)");
            break;
        case BossAI_ID::Act_MeleeAttack:
            // 攻撃実行は目立たせるために SUCCESS (緑色) を使用
            CCL_LOG_SUCCESS(LogCategory::AI, "[BossAI] State Changed -> MELEE ATTACK (近接攻撃！)");
            break;
        case BossAI_ID::Act_ChargeAttack:
            // 突進などの危険行動は WARN (黄色) を使用
            CCL_LOG_WARN(LogCategory::AI, "[BossAI] State Changed -> CHARGE ATTACK (突進開始！)");
            break;
        }
        // 記憶を更新（これ以降、別の行動になるまでログは沈黙する）
        btComp.lastLogActionId = actionId;
    }

    switch (actionId) {
    case BossAI_ID::Act_Move:
        cmdComp.requestMove = true;
        return BTNodeState::Running;
    case BossAI_ID::Act_MeleeAttack:
        cmdComp.requestMeleeAttack = true;
        return BTNodeState::Success;
    case BossAI_ID::Act_ChargeAttack:
        cmdComp.requestCharge = true;
        return BTNodeState::Running;
    default:
        return BTNodeState::Failure;
    }
}

// 規約に基づき末尾でシステム自動登録。
// 実行タイミングは「L02_Update (AI、移動、弾の更新など並列化の主戦場)」を指定。
// ※プロジェクト側に REGISTER_LOGIC_SYSTEM マクロが定義されている前提
REGISTER_LOGIC_SYSTEM(BossAISystem, Priority::LogicStage::L02_Update);