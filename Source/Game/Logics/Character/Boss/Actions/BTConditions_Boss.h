// ボス固有の条件。BossPhaseComponent（大状態）を見るものだけがここに入る。
//
// 汎用側（BTConditions.h）と分けている理由:
//   距離やHPは「どの敵でも意味がある」が、第2形態は1体のボスにしか無い概念。
//   同じファイルに置くと、敵を増やしたときにどれが汎用でどれが固有か分からなくなる。
//   BTActions_Boss.h と同じ考え方で、ボス固有のコンポーネントは
//   Execute/Evaluate の中で world から自分で取りに行く（BTContext は汎用のまま）。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Engine/ECS/Core/CCL_World.h"

namespace CCL::AI::BT::BossConditions
{
// 第2形態か。
//
// 以前は blackboard.currentPhase（int）を BossPhaseSystem が毎フレーム
// 同期して、それを読んでいた。大状態が唯一の情報源なので直接読む。
// 写し替えが1つ減るぶん、ズレようがなくなる。
struct IsPhase2 : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        const auto *phase = ctx.world->GetComponent<BossPhaseComponent>(ctx.entity);
        return phase && phase->phase == BossPhase::Combat2;
    }
};

// 第1形態で、HPが移行閾値を割っているか。
struct ShouldTransitionPhase2 : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        const auto *phase = ctx.world->GetComponent<BossPhaseComponent>(ctx.entity);
        if (!phase || phase->phase != BossPhase::Combat) return false;

        return ctx.bt->blackboard.healthPercentage <= phase->phase2HealthThreshold;
    }
};

inline void RegisterAll()
{
    auto &reg = BTActionRegistry::Instance();

    reg.RegisterCondition<IsPhase2>(::CCL::AI::C_IsPhase2);
    reg.RegisterCondition<ShouldTransitionPhase2>(::CCL::AI::C_ShouldTransitionPhase2);
}

} // namespace CCL::AI::BT::BossConditions
