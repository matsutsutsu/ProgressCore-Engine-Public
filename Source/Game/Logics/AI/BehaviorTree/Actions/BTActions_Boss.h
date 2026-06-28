/// @file  BTActions_Boss.h
/// @brief 既存 BossAISystem::ExecuteAction のロジックを IBTAction に移植したもの。
///        役割ごとに1ファイル（StateTree の EnemyAI.h と同じ方針）。
///
/// 元の挙動を完全再現するための4分類:
///   ① 待機系 WaitAction          : nodeTimers[nodeId] で計時 → 経過で Success
///   ② 即時系 ResetDamageAction   : 1フレームで Success
///   ③ 筋肉発注系 MuscleAction     : currentActionId を書き、終了で Success（共通基底で表現）
///   ④ ドローン系 DroneAction      : requestFormation を書いて即 Success
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/BossActionComponent.h"   // BossActionState, BossTimings
#include "Game/Logics/AI/BehaviorTree/Drone/DroneComponent.h"  // DroneFormationType


namespace CCL::AI::BT::BossActions
{
    // ====================================================================
    // ① 待機系（元: actionId が A_Wait0_5s〜A_Wait10_0s の分岐）
    //    nodeTimers の流儀：-1=未実行 / >0=計時中 / 0=完了
    //    ※減算は BossAISystem 側のループが行うので、ここはセットと判定のみ
    // ====================================================================
    struct WaitAction : public IBTAction
    {
        float duration;
        explicit WaitAction(float sec) : duration(sec) {}

        BTNodeState Execute(BTContext& ctx) override
        {
            auto& timers = ctx.bt->nodeTimers;
            if (ctx.nodeId >= timers.size()) return BTNodeState::Failure;

            if (timers[ctx.nodeId] == -1.0f)            // 未実行 → セット
                timers[ctx.nodeId] = duration;

            if (timers[ctx.nodeId] > 0.0f)              // 計時中
                return BTNodeState::Running;
            return BTNodeState::Success;                // 0 = 完了
        }
    };

    // ====================================================================
    // ② 即時系：蓄積ダメージのリセット（元: A_ResetDamage）
    // ====================================================================
    struct ResetDamageAction : public IBTAction
    {
        BTNodeState Execute(BTContext& ctx) override
        {
            ctx.bt->blackboard.accumulatedDamage = 0.0f;
            return BTNodeState::Success;
        }
    };

    // ====================================================================
    // ③ 筋肉発注系の共通基底
    //    元の「-1ならcurrentActionIdを書いてタイマーセット → 0でSuccess」を再現。
    //    Flinch中は発注せず Running で待つのも元の挙動どおり。
    //    派生クラスは「自分のActionID」と「タイマー長」を返すだけ。
    // ====================================================================
    struct MuscleAction : public IBTAction
    {
        virtual ActionID GetActionId() const = 0;
        virtual float    GetDuration(const BTContext& ctx) const = 0;

        BTNodeState Execute(BTContext& ctx) override
        {
            auto& timers = ctx.bt->nodeTimers;
            if (ctx.nodeId >= timers.size()) return BTNodeState::Failure;

            ctx.bt->lastLogActionId = GetActionId();

            if (timers[ctx.nodeId] == 0.0f)             // 完了
                return BTNodeState::Success;

            if (timers[ctx.nodeId] == -1.0f)            // 未実行 → 発注
            {
                // 怯み中は発注できない（元の挙動）
                if (ctx.myAction &&
                    ctx.myAction->currentState == BossActionState::Flinch)
                    return BTNodeState::Running;

                ctx.command->currentActionId = GetActionId();
                timers[ctx.nodeId] = GetDuration(ctx);
                return BTNodeState::Running;
            }
            return BTNodeState::Running;                // 実行中
        }
    };

    // --- 各筋肉アクション（タイマー長は元の ExecuteAction の対応を踏襲）---

    struct MeleeAttackAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_MeleeAttack; }
        float GetDuration(const BTContext&) const override { return BossTimings::Melee_Duration; }
    };
    struct ChargeAttackAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_ChargeAttack; }
        // 元: myAction.currentChargeTotal（形態で可変）
        float GetDuration(const BTContext& ctx) const override {
            return ctx.myAction ? ctx.myAction->currentChargeTotal : BossTimings::Charge_Total;
        }
    };
    struct JumpAttackAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_JumpAttack; }
        float GetDuration(const BTContext&) const override { return BossTimings::JumpAttack_Total; }
    };
    struct EvadeBackwardAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_EvadeBackward; }
        float GetDuration(const BTContext&) const override { return BossTimings::Evade_Total; }
    };
    struct JumpToHoverAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_JumpToHover; }
        float GetDuration(const BTContext&) const override { return BossTimings::JumpToHover_Total; }
    };
    struct HoverToLandAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_HoverToLand; }
        float GetDuration(const BTContext&) const override { return BossTimings::HoverToLand_Total; }
    };
    struct EscapeToCenterAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_EscapeToCenter; }
        float GetDuration(const BTContext&) const override { return BossTimings::EscapeToCenter_Total; }
    };
    struct StrafeLeftAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_StrafeLeft; }
        float GetDuration(const BTContext&) const override { return BossTimings::Strafe_Duration; }
    };
    struct StrafeRightAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_StrafeRight; }
        float GetDuration(const BTContext&) const override { return BossTimings::Strafe_Duration; }
    };
    struct StrafeRandomAction : public MuscleAction {
        ActionID GetActionId() const override { return AI::A_StrafeRandom; }
        float GetDuration(const BTContext&) const override { return BossTimings::Strafe_Duration; }
    };

    // --- Move は特殊（元: タイマー0でSuccess / Flinch以外は毎フレーム発注し続ける）---
    struct MoveAction : public IBTAction
    {
        BTNodeState Execute(BTContext& ctx) override
        {
            auto& timers = ctx.bt->nodeTimers;
            if (ctx.nodeId >= timers.size()) return BTNodeState::Failure;
            ctx.bt->lastLogActionId = AI::A_Move;

            if (timers[ctx.nodeId] == 0.0f) return BTNodeState::Success;
            if (timers[ctx.nodeId] == -1.0f) timers[ctx.nodeId] = 0.5f;  // 思考タイマー

            // 実行中は毎フレーム発注し続ける（Flinch中を除く）
            if (ctx.myAction &&
                ctx.myAction->currentState != BossActionState::Flinch)
                ctx.command->currentActionId = AI::A_Move;

            return BTNodeState::Running;
        }
    };

    // --- PhaseTransition も特殊（元: phase>=2でSuccess / 演出中Running）---
    struct PhaseTransitionAction : public IBTAction
    {
        BTNodeState Execute(BTContext& ctx) override
        {
            ctx.bt->lastLogActionId = AI::A_PhaseTransition;

            if (ctx.bt->blackboard.currentPhase >= 2)
                return BTNodeState::Success;

            if (ctx.myAction &&
                ctx.myAction->currentState != BossActionState::PhaseTransition &&
                ctx.myAction->currentState != BossActionState::Flinch)
                ctx.command->currentActionId = AI::A_PhaseTransition;

            return BTNodeState::Running;
        }
    };

    // ====================================================================
    // ④ ドローン系（元: 2000番台を requestFormation に変換して即 Success）
    //    元コードの「actionId - 2000 + 1 を Enum にキャスト」を踏襲。
    //    1クラスで全ドローン行動を表現できるので、IDだけ持たせる。
    // ====================================================================
    struct DroneAction : public IBTAction
    {
        ActionID id;
        explicit DroneAction(ActionID actionId) : id(actionId) {}

        BTNodeState Execute(BTContext& ctx) override
        {
            ctx.bt->lastLogActionId = id;
            ctx.command->requestFormation =
                static_cast<DroneFormationType>(id - 2000 + 1);
            return BTNodeState::Success;
        }
    };


    // ====================================================================
    // 登録：このファイルの全アクションをまとめて登録
    // ====================================================================
    inline void RegisterAll()
    {
        auto& reg = BTActionRegistry::Instance();

        // ① 待機（秒数違いを個別インスタンスで）
        reg.RegisterActionInstance(AI::A_Wait0_5s, std::make_unique<WaitAction>(0.5f));
        reg.RegisterActionInstance(AI::A_Wait1_0s, std::make_unique<WaitAction>(1.0f));
        reg.RegisterActionInstance(AI::A_Wait1_5s, std::make_unique<WaitAction>(1.5f));
        reg.RegisterActionInstance(AI::A_Wait2_0s, std::make_unique<WaitAction>(2.0f));
        reg.RegisterActionInstance(AI::A_Wait3_0s, std::make_unique<WaitAction>(3.0f));
        reg.RegisterActionInstance(AI::A_Wait4_0s, std::make_unique<WaitAction>(4.0f));
        reg.RegisterActionInstance(AI::A_Wait5_0s, std::make_unique<WaitAction>(5.0f));
        reg.RegisterActionInstance(AI::A_Wait6_0s, std::make_unique<WaitAction>(6.0f));
        reg.RegisterActionInstance(AI::A_Wait7_0s, std::make_unique<WaitAction>(7.0f));
        reg.RegisterActionInstance(AI::A_Wait8_0s, std::make_unique<WaitAction>(8.0f));
        reg.RegisterActionInstance(AI::A_Wait9_0s, std::make_unique<WaitAction>(9.0f));
        reg.RegisterActionInstance(AI::A_Wait10_0s, std::make_unique<WaitAction>(10.0f));

        // ② 即時
        reg.RegisterAction<ResetDamageAction>(AI::A_ResetDamage);

        // ③ 筋肉発注
        reg.RegisterAction<MeleeAttackAction>  (AI::A_MeleeAttack);
        reg.RegisterAction<ChargeAttackAction> (AI::A_ChargeAttack);
        reg.RegisterAction<JumpAttackAction>   (AI::A_JumpAttack);
        reg.RegisterAction<EvadeBackwardAction>(AI::A_EvadeBackward);
        reg.RegisterAction<JumpToHoverAction>  (AI::A_JumpToHover);
        reg.RegisterAction<HoverToLandAction>  (AI::A_HoverToLand);
        reg.RegisterAction<EscapeToCenterAction>(AI::A_EscapeToCenter);
        reg.RegisterAction<StrafeLeftAction>   (AI::A_StrafeLeft);
        reg.RegisterAction<StrafeRightAction>  (AI::A_StrafeRight);
        reg.RegisterAction<StrafeRandomAction> (AI::A_StrafeRandom);
        reg.RegisterAction<MoveAction>         (AI::A_Move);
        reg.RegisterAction<PhaseTransitionAction>(AI::A_PhaseTransition);

        // ④ ドローン（2000〜2029 を一括登録。1クラスでIDだけ変える）
        for (ActionID id = 2000; id <= 2029; ++id)
            reg.RegisterActionInstance(id, std::make_unique<DroneAction>(id));
    }

} // namespace CCL::AI::BT::BossActions
