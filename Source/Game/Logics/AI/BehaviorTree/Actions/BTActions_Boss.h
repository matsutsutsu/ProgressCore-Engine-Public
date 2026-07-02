/// @file  BTActions_Boss.h
/// @brief Drone Break 向け最小版。Move/Wait/Drone系のみ。
///        ボス本体の移動アクションは後で新規追加する。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Drone/DroneComponent.h"

// BossActionComponent は今後追加するので一旦前方宣言
struct BossActionComponent;

namespace CCL::AI::BT::BossActions
{
// ====================================================================
// ① 待機系
//    nodeTimers の流儀: -1=未実行 / >0=計時中 / 0=完了
//    タイマーの減算は BossAISystem 側が行うので、ここはセットと判定のみ
// ====================================================================
struct WaitAction : public IBTAction
{
    float duration;
    explicit WaitAction(float sec)
        : duration(sec)
    {
    }

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
// ② 即時系：蓄積ダメージのリセット
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
// ③ 移動（Move）
//    ボス本体の移動ロジックは BossActionSystem 側で実装する。
//    ここは「移動してほしい」という意図を currentActionId に書くだけ。
//    タイマー 0.5s で一旦 Success → BT に再評価させる。
// ====================================================================
struct MoveAction : public IBTAction
{
    BTNodeState Execute(BTContext &ctx) override
    {
        auto &timers = ctx.bt->nodeTimers;
        if (ctx.nodeId >= timers.size())
            return BTNodeState::Failure;

        ctx.bt->lastLogActionId = ::AI::A_Move;

        if (timers[ctx.nodeId] == 0.0f)
            return BTNodeState::Success;

        if (timers[ctx.nodeId] == -1.0f)
            timers[ctx.nodeId] = 0.5f;

        // 毎フレーム「移動してほしい」を書き続ける
        ctx.command->currentActionId = ::AI::A_Move;

        return BTNodeState::Running;
    }
};

// ====================================================================
// ④ ドローン系（2000番台 → DroneFormationType に変換して即 Success）
//    1クラスで全ドローン行動を表現。IDだけ違う。
// ====================================================================
struct DroneAction : public IBTAction
{
    ActionID id;
    explicit DroneAction(ActionID actionId)
        : id(actionId)
    {
    }

    BTNodeState Execute(BTContext &ctx) override
    {
        ctx.bt->lastLogActionId = id;
        ctx.command->requestFormation =
            static_cast<DroneFormationType>(id - 2000 + 1);
        return BTNodeState::Success;
    }
};

// ====================================================================
// 登録
// ====================================================================
inline void RegisterAll()
{
    auto &reg = BTActionRegistry::Instance();

    // ① 待機（秒数ごとにインスタンスを作って登録）
    reg.RegisterActionInstance(::AI::A_Wait0_5s, std::make_unique<WaitAction>(0.5f));
    reg.RegisterActionInstance(::AI::A_Wait1_0s, std::make_unique<WaitAction>(1.0f));
    reg.RegisterActionInstance(::AI::A_Wait1_5s, std::make_unique<WaitAction>(1.5f));
    reg.RegisterActionInstance(::AI::A_Wait2_0s, std::make_unique<WaitAction>(2.0f));
    reg.RegisterActionInstance(::AI::A_Wait3_0s, std::make_unique<WaitAction>(3.0f));
    reg.RegisterActionInstance(::AI::A_Wait4_0s, std::make_unique<WaitAction>(4.0f));
    reg.RegisterActionInstance(::AI::A_Wait5_0s, std::make_unique<WaitAction>(5.0f));
    reg.RegisterActionInstance(::AI::A_Wait6_0s, std::make_unique<WaitAction>(6.0f));
    reg.RegisterActionInstance(::AI::A_Wait7_0s, std::make_unique<WaitAction>(7.0f));
    reg.RegisterActionInstance(::AI::A_Wait8_0s, std::make_unique<WaitAction>(8.0f));
    reg.RegisterActionInstance(::AI::A_Wait9_0s, std::make_unique<WaitAction>(9.0f));
    reg.RegisterActionInstance(::AI::A_Wait10_0s, std::make_unique<WaitAction>(10.0f));

    // ② 即時
    reg.RegisterAction<ResetDamageAction>(::AI::A_ResetDamage);

    // ③ 移動
    reg.RegisterAction<MoveAction>(::AI::A_Move);

    // ④ ドローン（2000〜2029 を一括登録）
    for (ActionID id = 2000; id <= 2029; ++id)
        reg.RegisterActionInstance(id, std::make_unique<DroneAction>(id));
}

} // namespace CCL::AI::BT::BossActions