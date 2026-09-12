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

// 相手が正面 ±param 度の内側にいる（＝正面に捉えている）。param=90 で前方の半分。
// 角度も距離と同じく param で持つ。「正面」「側面」「背後」をIDにすると、
// 技ごとに欲しい角度が違うたびにIDが増え、中間の値も作れない。
struct TargetAngleLessThan : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return std::fabs(ctx.bt->blackboard.angleToTarget) < ctx.param;
    }
};

// 相手が正面 ±param 度の外側にいる（＝回り込まれている）。param=120 で背後まわり。
struct TargetAngleGreaterThan : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return std::fabs(ctx.bt->blackboard.angleToTarget) > ctx.param;
    }
};

// 相手が param m/s 以上の速さで詰めてきている。踏み込みを咎める置き技の引き金。
struct TargetApproaching : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.targetClosingSpeed >= ctx.param;
    }
};

// 相手が param m/s 以上の速さで離れている。距離を取って粘る相手を咎めるのに使う。
// 符号を反転して比べるのは、閾値を負で書かせると木の上で読みにくいため。
struct TargetRetreating : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return -ctx.bt->blackboard.targetClosingSpeed >= ctx.param;
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
    reg.RegisterCondition<TargetAngleLessThan>(::CCL::AI::C_TargetAngleLessThan);
    reg.RegisterCondition<TargetAngleGreaterThan>(::CCL::AI::C_TargetAngleGreaterThan);
    reg.RegisterCondition<TargetApproaching>(::CCL::AI::C_TargetApproaching);
    reg.RegisterCondition<TargetRetreating>(::CCL::AI::C_TargetRetreating);
    reg.RegisterCondition<DamageThresholdExceeded>(::CCL::AI::C_DamageThresholdExceeded);
    reg.RegisterCondition<IsAtStageEdge>(::CCL::AI::C_IsAtStageEdge);
}

} // namespace CCL::AI::BT::CommonConditions
