/**
 * BehaviorTree のアクション・条件の基底。
 *
 * 汎用化（ボス固有を排除）
 *   BTContext からボス固有の command(BossCommand) / myAction(BossAction) を外した。
 *   これらが必要なアクション（ボス用）は Execute の中で
 *   ctx.world->GetComponent<BossCommandComponent>(ctx.entity) と自分で取りに行く。
 *   → BTエンジンとContextが「ボス」を一切知らなくなり、ドローンにも流用できる。
 */
#pragma once
#include <cstdint>

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"        // BTNodeState, ActionID, BTNodeID
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"  // BehaviorTreeComponent
#include "Engine/ECS/Common/CCL_Types.h"

// 前方宣言
namespace CCL::ECS { class World; }
struct TransformComponent;


namespace CCL::AI::BT
{
    // ====================================================================
    // BTContext — アクション/条件が受け取る情報のカバン（汎用のみ）
    // ====================================================================
    /// どのエンティティのBTでも使える汎用データだけを持つ。
    ///        ボス固有のコンポーネントは world/entity から自分で取得する。
    struct BTContext
    {
        CCL::ECS::World* world  = nullptr;
        CCL::ECS::EntityID     entity = 0;

        // --- AI状態（nodeTimers / blackboard / lastLogActionId を読み書き）---
        BehaviorTreeComponent* bt = nullptr;

        // --- 読み取り専用データ（どのAIも持ちうる汎用コンポーネント）---
        const TransformComponent* myTransform = nullptr;

        // --- ノード情報 ---
        BTNodeID nodeId = 0;   // nodeTimers[nodeId] を引くのに使う
        float    dt     = 0.0f;

        /// このノードに設定された数値パラメータ（BTNode::param）。
        ///        待機の秒数や近接の硬直など、行動の引数をここから受け取る。
        ///        使わない行動は無視してよい（既定 0）。
        float    param  = 0.0f;
    };


    // ====================================================================
    // IBTAction — 行動の基底
    // ====================================================================
    struct IBTAction
    {
        virtual ~IBTAction() = default;
        /// 1フレームの行動。Success / Failure / Running を返す。
        virtual BTNodeState Execute(BTContext& ctx) = 0;
    };

    // ====================================================================
    // IBTCondition — 条件の基底
    // ====================================================================
    struct IBTCondition
    {
        virtual ~IBTCondition() = default;
        virtual bool Evaluate(BTContext& ctx) = 0;
    };

} // namespace CCL::AI::BT
