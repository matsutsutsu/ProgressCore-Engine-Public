// どの敵でも使える汎用条件。ボス固有の条件は BTConditions_Boss.h。
//
// 間合いをIDではなく param で持つのは、距離を条件クラスに即値で埋めると
// 「敵の種類 × 間合い」の数だけ ActionID が増え、中間の値も作れないため。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include <cmath>

namespace CCL::AI::BT
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

    // ステージの端から param m 以内に居る。param=0 で「壁際まで来た」。
    // 円の中心と半径は BattleContext が持つので、ステージを差し替えても木は直さなくてよい。
    struct IsAtStageEdge : public IBTCondition
    {
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.distanceToArenaEdge <= ctx.param;
        }
    };

    // 相手が「自分から見た中央の方向」±param 度の内側に立っている（逃げ道を塞がれている）。
    // C_IsAtStageEdge と Sequence で並べて「追い込まれた」を作る。param=60 で中央側の扇。
    struct TargetOnCenterSide : public IBTCondition
    {
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.targetAngleFromCenter <= ctx.param;
        }
    };

    /// どの敵でも使う条件を登録する。起動時に1回だけ呼ぶ。
    inline void RegisterCommonConditions()
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
        reg.RegisterCondition<TargetOnCenterSide>(::CCL::AI::C_TargetOnCenterSide);
    }

} // namespace CCL::AI::BT
