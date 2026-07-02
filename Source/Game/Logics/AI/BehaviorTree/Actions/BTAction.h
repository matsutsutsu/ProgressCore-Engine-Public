/// @file  BTAction.h
/// @brief BehaviorTree のアクション・条件の基底。StateTree の ITask/ICondition と対。
///
/// 既存 BossAISystem::ExecuteAction の挙動を完全再現するため、BTContext は
/// nodeTimers を含む BehaviorTreeComponent 全体への参照を持つ。
#pragma once
#include <cstdint>

#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"        // BTNodeState, ActionID, BTNodeID
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"  // BehaviorTreeComponent, BossCommandComponent

// 前方宣言
namespace CCL::ECS::Core { class World; }
namespace CCL::ECS        { using EntityID = unsigned long long; }
struct TransformComponent;
struct BossActionComponent;


namespace CCL::AI::BT
{
    // ====================================================================
    // BTContext — アクション/条件が受け取る情報のカバン
    // ====================================================================
    /// @brief 既存 ExecuteAction の引数群をそのまま1つにまとめたもの。
    struct BTContext
    {
        CCL::ECS::Core::World* world  = nullptr;
        CCL::ECS::EntityID     entity = 0;

        // --- AI状態（nodeTimers / blackboard / lastLogActionId を読み書き）---
        BehaviorTreeComponent* bt = nullptr;

        // --- 出力先（currentActionId / requestFormation を書く）---
        BossCommandComponent*  command = nullptr;

        // --- 読み取り専用データ ---
        const TransformComponent*  myTransform = nullptr;
        const BossActionComponent* myAction    = nullptr;

        // --- ノード情報 ---
        BTNodeID nodeId = 0;   // nodeTimers[nodeId] を引くのに使う
        float    dt     = 0.0f;
    };


    // ====================================================================
    // IBTAction — 行動の基底
    // ====================================================================
    struct IBTAction
    {
        virtual ~IBTAction() = default;
        /// @brief 1フレームの行動。Success / Failure / Running を返す。
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
