/// @file  BTActions_Boss.h
/// @brief Drone Break 向け最小版。Move/Wait/Drone系のみ。
///        ★ボス固有のコンポーネント(BossCommand)は BTContext から受け取らず、
///          各アクションが world/entity から自分で取得する（汎用エンジン化のため）。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Engine/ECS/Core/CCL_World.h"                               // world->GetComponent


namespace CCL::AI::BT::BossActions
{
// ====================================================================
// ① 待機系（汎用：bt の nodeTimers だけ使う。ボス非依存）
// ====================================================================
struct WaitAction : public IBTAction
{
    float duration;
    explicit WaitAction(float sec) : duration(sec) {}

    BTNodeState Execute(BTContext &ctx) override
    {
        auto &timers = ctx.bt->nodeTimers;
        if (ctx.nodeId >= timers.size())
            return BTNodeState::Failure;

        if (timers[ctx.nodeId] == -1.0f)
            timers[ctx.nodeId] = duration;

        if (timers[ctx.nodeId] > 0.0f)
            return BTNodeState::Running;

        return BTNodeState::Success;
    }
};

// ====================================================================
// ② 即時系：蓄積ダメージのリセット（汎用：bt のみ）
// ====================================================================
struct ResetDamageAction : public IBTAction
{
    BTNodeState Execute(BTContext &ctx) override
    {
        ctx.bt->blackboard.accumulatedDamage = 0.0f;
        return BTNodeState::Success;
    }
};

// ====================================================================
// ③ 移動（Move）★ボス固有：BossCommand を自分で取りに行く
//    「移動してほしい」意図を currentActionId に書く。
//    タイマー 0.5s で一旦 Success させ、BT に再評価させる。
// ====================================================================
struct MoveAction : public IBTAction
{
    BTNodeState Execute(BTContext &ctx) override
    {
        auto &timers = ctx.bt->nodeTimers;
        if (ctx.nodeId >= timers.size())
            return BTNodeState::Failure;

        ctx.bt->lastLogActionId = ::AI::A_Move;

        // 完了
        if (timers[ctx.nodeId] == 0.0f)
            return BTNodeState::Success;

        // 未実行 → タイマー設定
        if (timers[ctx.nodeId] == -1.0f)
            timers[ctx.nodeId] = 0.5f;

        // 実行中：ボス固有の出力先を自分で取得して書く
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity)) {
            cmd->currentActionId = ::AI::A_Move;
        }
        return BTNodeState::Running;
    }
};

// ====================================================================
// ④ ドローン系（2000番台）★ボス固有：BossCommand を自分で取りに行く
// ====================================================================
struct DroneAction : public IBTAction
{
    ActionID id;
    explicit DroneAction(ActionID actionId) : id(actionId) {}

    BTNodeState Execute(BTContext &ctx) override
    {
        ctx.bt->lastLogActionId = id;

        // ボス固有の出力先（requestFormation）を自分で取得して書く
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity)) {
            cmd->requestFormation =
                static_cast<DroneFormationType>(id - 2000 + 1);
        }
        return BTNodeState::Success;
    }
};

// ====================================================================
// 登録
// ====================================================================
inline void RegisterAll()
{
    auto &reg = BTActionRegistry::Instance();

    reg.RegisterActionInstance(::AI::A_Wait0_5s,  std::make_unique<WaitAction>(0.5f));
    reg.RegisterActionInstance(::AI::A_Wait1_0s,  std::make_unique<WaitAction>(1.0f));
    reg.RegisterActionInstance(::AI::A_Wait1_5s,  std::make_unique<WaitAction>(1.5f));
    reg.RegisterActionInstance(::AI::A_Wait2_0s,  std::make_unique<WaitAction>(2.0f));
    reg.RegisterActionInstance(::AI::A_Wait3_0s,  std::make_unique<WaitAction>(3.0f));
    reg.RegisterActionInstance(::AI::A_Wait4_0s,  std::make_unique<WaitAction>(4.0f));
    reg.RegisterActionInstance(::AI::A_Wait5_0s,  std::make_unique<WaitAction>(5.0f));
    reg.RegisterActionInstance(::AI::A_Wait6_0s,  std::make_unique<WaitAction>(6.0f));
    reg.RegisterActionInstance(::AI::A_Wait7_0s,  std::make_unique<WaitAction>(7.0f));
    reg.RegisterActionInstance(::AI::A_Wait8_0s,  std::make_unique<WaitAction>(8.0f));
    reg.RegisterActionInstance(::AI::A_Wait9_0s,  std::make_unique<WaitAction>(9.0f));
    reg.RegisterActionInstance(::AI::A_Wait10_0s, std::make_unique<WaitAction>(10.0f));

    reg.RegisterAction<ResetDamageAction>(::AI::A_ResetDamage);
    reg.RegisterAction<MoveAction>(::AI::A_Move);

    for (ActionID id = 2000; id <= 2029; ++id)
        reg.RegisterActionInstance(id, std::make_unique<DroneAction>(id));
}

} // namespace CCL::AI::BT::BossActions