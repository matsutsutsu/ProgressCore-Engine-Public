// どの敵でも使える汎用条件。ボス固有の条件は BTConditions_Boss.h。
//
// 間合いをIDではなく param で持つのは、距離を条件クラスに即値で埋めると
// 「敵の種類 × 間合い」の数だけ ActionID が増え、中間の値も作れないため。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include <cmath>

namespace CCL::AI::BT::CommonConditions
{
// ターゲットとの距離が param 未満。
struct DistanceLessThan : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.distanceToTarget < ctx.param;
    }
};

// ターゲットとの距離が param より大きい。
struct DistanceGreaterThan : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.distanceToTarget > ctx.param;
    }
};

// 前回リセットしてからの被弾が、最大HPの param % に達した（未設定なら 20%）。
// 閾値をノードに置くと、形態ごと・敵ごとに退く頻度を変えてもIDが増えない。
struct DamageThresholdExceeded : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        const float thresholdPercent = (ctx.param > 0.0f) ? ctx.param : 20.0f;
        return ctx.bt->blackboard.accumulatedDamageRatio >= thresholdPercent;
    }
};

// 原点から param m 以上離れている（ステージの端に寄っている）。未設定なら 48m。
struct IsAtStageEdge : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        if (!ctx.myTransform) return false;

        const float radius = (ctx.param > 0.0f) ? ctx.param : 48.0f;
        const float x = ctx.myTransform->position.x;
        const float z = ctx.myTransform->position.z;
        return std::sqrt(x * x + z * z) >= radius;
    }
};

inline void RegisterAll()
{
    auto &reg = BTActionRegistry::Instance();

    reg.RegisterCondition<DistanceLessThan>(::CCL::AI::C_DistanceLessThan);
    reg.RegisterCondition<DistanceGreaterThan>(::CCL::AI::C_DistanceGreaterThan);
    reg.RegisterCondition<DamageThresholdExceeded>(::CCL::AI::C_DamageThresholdExceeded);
    reg.RegisterCondition<IsAtStageEdge>(::CCL::AI::C_IsAtStageEdge);
}

} // namespace CCL::AI::BT::CommonConditions
