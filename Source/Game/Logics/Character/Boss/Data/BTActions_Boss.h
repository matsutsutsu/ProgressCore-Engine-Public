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
#include "Engine/Animation/Components/AnimParametersComponent.h"     // SetTrigger（近接アニメ）
#include "Engine/Core/Math/StringHash.h"                             // HashString


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

        ctx.bt->lastLogActionId = ::CCL::AI::A_Move;

        // 完了
        if (timers[ctx.nodeId] == 0.0f)
            return BTNodeState::Success;

        // 未実行 → タイマー設定
        if (timers[ctx.nodeId] == -1.0f)
            timers[ctx.nodeId] = 0.5f;

        // 実行中：ボス固有の出力先を自分で取得して書く
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity)) {
            cmd->currentActionId = ::CCL::AI::A_Move;
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

            // 陣形の指示 = 「形を作る（予備動作/帰投）」フェーズなので攻撃信号は下ろす。
            // 突撃（リリース）は後続の DroneStrikeAction が明示的に立てる。
            cmd->droneAttackActive = false;
        }
        return BTNodeState::Success;
    }
};

// ====================================================================
// ⑤ ドローン攻撃リリース（Strike）★ボス固有：BossCommand を自分で取りに行く
//    「今撃て」信号を立てる。DroneCommandSystem 経由で各ドローンが FireCharge へ。
// ====================================================================
struct DroneStrikeAction : public IBTAction
{
    BTNodeState Execute(BTContext &ctx) override
    {
        ctx.bt->lastLogActionId = ::CCL::AI::A_DroneStrike;
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity)) {
            cmd->droneAttackActive = true;
        }
        return BTNodeState::Success;
    }
};

// ====================================================================
// ⑥ 近接攻撃（Melee）★ボス固有：アニメのトリガーを引く
//    設計：AIは「攻撃を決めてアニメのトリガーを引く」だけ。実際の当たり判定の
//    タイミングは、アニメクリップ内の HitBox イベント(startTime/endTime)が握る
//    （CombatAnimationSyncSystem が剣 Hitbox を区間ON/OFFする）。
//    このアクションは:
//      - 開始フレームに1回だけ SetTrigger（＝攻撃発生の合図）
//      - duration の間 Running（振り＋硬直ぶんBTを留める＝連打防止）
//      - currentActionId を立て続け、BossActionSystem が足を止める/踏み込む等の
//        物理挙動を担当する（薙ぎ払いは停止、将来の踏み込み/突進は前進）。
//    ★前提：ボスに AnimParameters コンポーネントと、triggerHash に対応する
//      遷移（例 "Slash"）が AnimStateMachine に用意されていること。
// ====================================================================
struct MeleeAttackAction : public IBTAction
{
    uint32_t triggerHash;
    float    duration;
    ActionID id;

    MeleeAttackAction(uint32_t hash, float dur, ActionID actionId)
        : triggerHash(hash), duration(dur), id(actionId) {}

    BTNodeState Execute(BTContext &ctx) override
    {
        auto &timers = ctx.bt->nodeTimers;
        if (ctx.nodeId >= timers.size()) return BTNodeState::Failure;

        ctx.bt->lastLogActionId = id;

        // 硬直終了 → 完了
        if (timers[ctx.nodeId] == 0.0f) return BTNodeState::Success;

        // 開始フレーム：アニメのトリガーを1回だけ引く（＝攻撃発生の合図）
        if (timers[ctx.nodeId] == -1.0f)
        {
            timers[ctx.nodeId] = duration;
            if (auto *anim = ctx.world->GetComponent<AnimParametersComponent>(ctx.entity))
                anim->SetTrigger(triggerHash);
        }

        // 硬直中：近接中であることを BossActionSystem へ伝える（停止/前進の制御に使う）
        if (auto *cmd = ctx.world->GetComponent<BossCommandComponent>(ctx.entity))
            cmd->currentActionId = id;

        return BTNodeState::Running;
    }
};

// ====================================================================
// 登録
// ====================================================================
inline void RegisterAll()
{
    auto &reg = BTActionRegistry::Instance();

    // 近接：薙ぎ払い（アニメのトリガー名 "Slash" / 硬直 0.8s）
    reg.RegisterActionInstance(::CCL::AI::A_BossSlash,
        std::make_unique<MeleeAttackAction>(CCL::Utils::HashString("Slash"), 0.8f, ::CCL::AI::A_BossSlash));

    reg.RegisterActionInstance(::CCL::AI::A_Wait0_5s,  std::make_unique<WaitAction>(0.5f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait1_0s,  std::make_unique<WaitAction>(1.0f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait1_5s,  std::make_unique<WaitAction>(1.5f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait2_0s,  std::make_unique<WaitAction>(2.0f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait3_0s,  std::make_unique<WaitAction>(3.0f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait4_0s,  std::make_unique<WaitAction>(4.0f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait5_0s,  std::make_unique<WaitAction>(5.0f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait6_0s,  std::make_unique<WaitAction>(6.0f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait7_0s,  std::make_unique<WaitAction>(7.0f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait8_0s,  std::make_unique<WaitAction>(8.0f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait9_0s,  std::make_unique<WaitAction>(9.0f));
    reg.RegisterActionInstance(::CCL::AI::A_Wait10_0s, std::make_unique<WaitAction>(10.0f));

    reg.RegisterAction<ResetDamageAction>(::CCL::AI::A_ResetDamage);
    reg.RegisterAction<MoveAction>(::CCL::AI::A_Move);
    reg.RegisterAction<DroneStrikeAction>(::CCL::AI::A_DroneStrike);

    for (ActionID id = ::CCL::AI::A_DroneFirst; id <= ::CCL::AI::A_DroneLast; ++id)
        reg.RegisterActionInstance(id, std::make_unique<DroneAction>(id));
}

} // namespace CCL::AI::BT::BossActions