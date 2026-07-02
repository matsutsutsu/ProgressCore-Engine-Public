/// @file  BTConditions.h
/// @brief BTの条件クラス群。::AI::C_〜 でグローバルの ActionRegistry を参照。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include <cmath>

namespace CCL::AI::BT::BossConditions
{
// 距離 < 15m
struct RangeClose : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.distanceToTarget < 15.0f;
    }
};

// 距離 15〜40m
struct RangeMedium : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        float d = ctx.bt->blackboard.distanceToTarget;
        return d >= 15.0f && d < 40.0f;
    }
};

// 距離 < 8m
struct RangeMostClose : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.distanceToTarget < 8.0f;
    }
};

// 距離 > 40m
struct RangeFar : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.distanceToTarget > 40.0f;
    }
};

// フェーズ >= 2
struct IsPhase2 : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.currentPhase >= 2;
    }
};

// 蓄積ダメージ >= 閾値
struct DamageThresholdExceeded : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.accumulatedDamage >= ctx.bt->blackboard.evadeDamageThreshold;
    }
};

// 第1形態 & HP <= 閾値
struct ShouldTransitionPhase2 : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return (ctx.bt->blackboard.currentPhase == 1) &&
               (ctx.bt->blackboard.healthPercentage <= ctx.bt->phase2HealthThreshold);
    }
};

// 原点から 48m 以上離れている
struct IsAtStageEdge : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        if (!ctx.myTransform)
            return false;
        float x = ctx.myTransform->position.x;
        float z = ctx.myTransform->position.z;
        return std::sqrt(x * x + z * z) >= 48.0f;
    }
};

inline void RegisterAll()
{
    auto &reg = BTActionRegistry::Instance();

    // ::AI:: でグローバルの ActionRegistry の ID を参照
    reg.RegisterCondition<RangeClose>(::AI::C_RangeClose);
    reg.RegisterCondition<RangeMedium>(::AI::C_RangeMedium);
    reg.RegisterCondition<RangeMostClose>(::AI::C_RangeMostClose);
    reg.RegisterCondition<RangeFar>(::AI::C_RangeFar);
    reg.RegisterCondition<IsPhase2>(::AI::C_IsPhase2);
    reg.RegisterCondition<DamageThresholdExceeded>(::AI::C_DamageThresholdExceeded);
    reg.RegisterCondition<ShouldTransitionPhase2>(::AI::C_ShouldTransitionPhase2);
    reg.RegisterCondition<IsAtStageEdge>(::AI::C_IsAtStageEdge);
}

} // namespace CCL::AI::BT::BossConditions