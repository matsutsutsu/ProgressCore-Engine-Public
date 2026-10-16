#pragma once
// ===========================================================================
// 概要: どの敵でも使える汎用の BT 条件（距離・角度・接近の速さ・被弾量・ステージの端）。
//
// 責務:
//   - blackboard の値とノードの param を比べる条件を定義する
//   - それらを BTActionRegistry へ登録する（RegisterCommonConditions）
//
// 備考: ボス固有の条件は BTConditions_Boss.h。
//       間合いを ID ではなく param で持つのは、距離を条件クラスに定数で埋めると
//       「敵の種類 × 間合い」の数だけ ActionID が増え、中間の値も作れないため。
// ===========================================================================
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
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

    // 相手が正面 ±param 度の内側にいる（正面に捉えている）。param=90 で前方の半分。
    // 角度も距離と同じく param で持つ。「正面」「側面」「背後」を ID にすると、
    // 技ごとに欲しい角度が違うたびに ID が増え、中間の値も作れない。
    struct TargetAngleLessThan : public IBTCondition
    {
        bool Evaluate(BTContext &ctx) override
        {
            return std::fabs(ctx.bt->blackboard.angleToTarget) < ctx.param;
        }
    };

    // 相手が正面 ±param 度の外側にいる（回り込まれている）。param=120 で背後側。
    struct TargetAngleGreaterThan : public IBTCondition
    {
        bool Evaluate(BTContext &ctx) override
        {
            return std::fabs(ctx.bt->blackboard.angleToTarget) > ctx.param;
        }
    };

    // 相手が param m/s 以上の速さで近づいてきている。踏み込んでくる相手に合わせて出す技の条件に使う。
    struct TargetApproaching : public IBTCondition
    {
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.targetClosingSpeed >= ctx.param;
        }
    };

    // 相手が param m/s 以上の速さで離れている。距離を取り続ける相手への技の条件に使う。
    // 符号を反転して比べるのは、しきい値を負の数で書かせると木の上で読みにくいため。
    struct TargetRetreating : public IBTCondition
    {
        bool Evaluate(BTContext &ctx) override
        {
            return -ctx.bt->blackboard.targetClosingSpeed >= ctx.param;
        }
    };

    // 前回リセットしてからの被弾が、最大 HP の param % に達した（未設定なら 20%）。
    // しきい値をノードに置くので、形態ごと・敵ごとに退く頻度を変えても ID が増えない。
    struct DamageThresholdExceeded : public IBTCondition
    {
        bool Evaluate(BTContext &ctx) override
        {
            const float thresholdPercent = (ctx.param > 0.0f) ? ctx.param : 20.0f;
            return ctx.bt->blackboard.accumulatedDamageRatio >= thresholdPercent;
        }
    };

    // ステージの端から param m 以内に居る。param=0 で壁際まで来たとき。
    // 円の中心と半径は BattleContext が持つので、ステージを変えても木は直さなくてよい。
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

    // どの敵でも使う条件を登録する。起動時に1回だけ呼ぶ。
    inline void RegisterCommonConditions()
    {
        auto &reg = BTActionRegistry::Instance();

        reg.RegisterCondition<DistanceLessThan>(C_DistanceLessThan);
        reg.RegisterCondition<DistanceGreaterThan>(C_DistanceGreaterThan);
        reg.RegisterCondition<TargetAngleLessThan>(C_TargetAngleLessThan);
        reg.RegisterCondition<TargetAngleGreaterThan>(C_TargetAngleGreaterThan);
        reg.RegisterCondition<TargetApproaching>(C_TargetApproaching);
        reg.RegisterCondition<TargetRetreating>(C_TargetRetreating);
        reg.RegisterCondition<DamageThresholdExceeded>(C_DamageThresholdExceeded);
        reg.RegisterCondition<IsAtStageEdge>(C_IsAtStageEdge);
        reg.RegisterCondition<TargetOnCenterSide>(C_TargetOnCenterSide);
    }

} // namespace CCL::AI::BT
