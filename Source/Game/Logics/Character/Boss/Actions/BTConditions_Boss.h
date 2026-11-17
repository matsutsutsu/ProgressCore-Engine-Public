#pragma once
// ===========================================================================
// 概要: ボス固有の BT 条件（BossPhaseComponent の形態を見るもの）
//
// 責務:
//   - 第2形態かを判定する（IsPhase2）
//   - 第1形態で HP が移行のしきい値以下かを判定する（ShouldTransitionPhase2）
//   - これらを BTActionRegistry へ登録する
//
// 備考: 距離や HP はどの敵でも意味があるが、
//       第2形態はボスにしかないので分けている
//       BTActions_Boss.cpp と同じく、固有の Component は world から自分で取る
// ===========================================================================
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Engine/ECS/Core/CCL_World.h"

namespace CCL::AI::BT
{
    // 第2形態か
    // 形態は BossPhaseComponent だけが持つので、blackboard へ写さず直接読む
    class IsPhase2 : public IBTCondition
    {
    public:
        std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const override
        {
            return TypeIDsOf<BossPhaseComponent>();
        }

        bool Evaluate(BTContext &ctx) override
        {
            const auto *phase = ctx.world->GetComponent<BossPhaseComponent>(ctx.entity);
            return phase && phase->phase == BossPhase::Combat2;
        }
    };

    // 第1形態で、HP が移行のしきい値以下か
    class ShouldTransitionPhase2 : public IBTCondition
    {
    public:
        std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const override
        {
            return TypeIDsOf<BossPhaseComponent>();
        }

        bool Evaluate(BTContext &ctx) override
        {
            const auto *phase = ctx.world->GetComponent<BossPhaseComponent>(ctx.entity);
            if (!phase) return false;
            if (phase->phase != BossPhase::Combat) return false;

            return ctx.bt->blackboard.healthPercentage <= phase->phase2HealthThreshold;
        }
    };

    // ボス固有の条件を登録する。起動時に1回だけ呼ぶ
    inline void RegisterBossConditions()
    {
        auto &reg = BTActionRegistry::Instance();

        reg.RegisterCondition<IsPhase2>(C_IsPhase2);
        reg.RegisterCondition<ShouldTransitionPhase2>(C_ShouldTransitionPhase2);
    }

}
