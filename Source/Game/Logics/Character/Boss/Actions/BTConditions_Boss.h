// ボス固有の条件。BossPhaseComponent（大状態）を見るものだけがここに入る。
// 距離やHPはどの敵でも意味があるが、第2形態は1体のボスにしか無い概念なので分ける。
// BTActions_Boss.h と同じく、固有のコンポーネントは world から自分で取りに行く。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/Character/Boss/Components/BossPhaseComponent.h"
#include "Engine/ECS/Core/CCL_World.h"

namespace CCL::AI::BT::BossConditions
{
// 第2形態か。大状態が唯一の情報源なので blackboard へ写さず直接読む。
struct IsPhase2 : public IBTCondition
{
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

// 第1形態で、HPが移行閾値を割っているか。
struct ShouldTransitionPhase2 : public IBTCondition
{
    std::vector<CCL::ECS::TypeID> GetExtraReadTypes() const override
    {
        return TypeIDsOf<BossPhaseComponent>();
    }

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
