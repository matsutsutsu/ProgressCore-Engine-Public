/// @file  BTConditions.h
/// @brief 既存 BossAISystem::EvaluateCondition を IBTCondition に移植。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include <cmath>


namespace CCL::AI::BT::BossConditions
{
    struct RangeClose : public IBTCondition {   // 距離 < 15m
        bool Evaluate(BTContext& ctx) override {
            return ctx.bt->blackboard.distanceToTarget < 15.0f;
        }
    };
    struct RangeMedium : public IBTCondition {  // 15〜40m
        bool Evaluate(BTContext& ctx) override {
            float d = ctx.bt->blackboard.distanceToTarget;
            return d >= 15.0f && d < 40.0f;
        }
    };
    struct RangeMostClose : public IBTCondition { // 距離 < 8m
        bool Evaluate(BTContext& ctx) override {
            return ctx.bt->blackboard.distanceToTarget < 8.0f;
        }
    };
    struct RangeFar : public IBTCondition {     // 距離 > 40m
        bool Evaluate(BTContext& ctx) override {
            return ctx.bt->blackboard.distanceToTarget > 40.0f;
        }
    };
    struct IsPhase2 : public IBTCondition {     // フェーズ >= 2
        bool Evaluate(BTContext& ctx) override {
            return ctx.bt->blackboard.currentPhase >= 2;
        }
    };
    struct DamageThresholdExceeded : public IBTCondition {  // 蓄積ダメージ >= 閾値
        bool Evaluate(BTContext& ctx) override {
            return ctx.bt->blackboard.accumulatedDamage
                >= ctx.bt->blackboard.evadeDamageThreshold;
        }
    };
    struct ShouldTransitionPhase2 : public IBTCondition {   // 第1形態 & HP<=閾値
        bool Evaluate(BTContext& ctx) override {
            return (ctx.bt->blackboard.currentPhase == 1) &&
                   (ctx.bt->blackboard.healthPercentage <= ctx.bt->phase2HealthThreshold);
        }
    };
    struct IsAtStageEdge : public IBTCondition {  // 原点から48m以上
        bool Evaluate(BTContext& ctx) override {
            if (!ctx.myTransform) return false;
            float x = ctx.myTransform->position.x;
            float z = ctx.myTransform->position.z;
            return std::sqrt(x * x + z * z) >= 48.0f;
        }
    };

    inline void RegisterAll()
    {
        auto& reg = BTActionRegistry::Instance();
        reg.RegisterCondition<RangeClose>            (AI::C_RangeClose);
        reg.RegisterCondition<RangeMedium>           (AI::C_RangeMedium);
        reg.RegisterCondition<RangeMostClose>        (AI::C_RangeMostClose);
        reg.RegisterCondition<RangeFar>              (AI::C_RangeFar);
        reg.RegisterCondition<IsPhase2>              (AI::C_IsPhase2);
        reg.RegisterCondition<DamageThresholdExceeded>(AI::C_DamageThresholdExceeded);
        reg.RegisterCondition<ShouldTransitionPhase2>(AI::C_ShouldTransitionPhase2);
        reg.RegisterCondition<IsAtStageEdge>         (AI::C_IsAtStageEdge);
    }

} // namespace CCL::AI::BT::BossConditions
