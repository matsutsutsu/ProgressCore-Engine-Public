#pragma once
// ===========================================================================
// 概要: どの敵でも使える汎用の BT 条件（距離・角度・接近や横の速さ・被弾・ステージの端）。
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
    class DistanceLessThan : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.distanceToTarget < ctx.param;
        }
    };

    // ターゲットとの距離が param より大きい。
    class DistanceGreaterThan : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.distanceToTarget > ctx.param;
        }
    };

    // 相手が正面 ±param 度の内側にいる（正面に捉えている）。param=90 で前方の半分
    class TargetAngleLessThan : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return std::fabs(ctx.bt->blackboard.angleToTarget) < ctx.param;
        }
    };

    // 相手が正面 ±param 度の外側にいる（回り込まれている）。param=120 で背後側。
    class TargetAngleGreaterThan : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return std::fabs(ctx.bt->blackboard.angleToTarget) > ctx.param;
        }
    };

    // 相手が param m/s 以上の速さで近づいてきている。踏み込んでくる相手に合わせて出す技の条件に使う。
    class TargetApproaching : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.targetClosingSpeed >= ctx.param;
        }
    };

    // 相手が param m/s 以上の速さで離れている。param は正の数で書く（負の数は木の上で読みにくい）
    class TargetRetreating : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return -ctx.bt->blackboard.targetClosingSpeed >= ctx.param;
        }
    };

    // 相手が param m/s 以上の速さで、自分の周りを横へ回っている。横に動き回る相手に合わせて出す技の条件に使う。
    class TargetStrafing : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.targetLateralSpeed >= ctx.param;
        }
    };

    // 相手の速さが param m/s 未満（止まりぎみ）。その場に留まる相手に合わせて出す技の条件に使う。
    class TargetSlow : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.targetSpeed < ctx.param;
        }
    };

    // 前回リセットしてからの被弾が、最大 HP の param % に達した（未設定なら 20%）
    class DamageThresholdExceeded : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            const float thresholdPercent = (ctx.param > 0.0f) ? ctx.param : 20.0f;
            return ctx.bt->blackboard.accumulatedDamageRatio >= thresholdPercent;
        }
    };

    // ステージの端から param m 以内に居る。param=0 で壁際まで来たとき
    class IsAtStageEdge : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.distanceToArenaEdge <= ctx.param;
        }
    };

    // 相手が「自分から見た中央の方向」±param 度の内側にいる（逃げ道を塞がれている）。C_IsAtStageEdge と並べて「追い込まれた」を作る
    class TargetOnCenterSide : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.targetAngleFromCenter <= ctx.param;
        }
    };

    // 追い込まれている：ステージの端から param m 以内に居て、相手が中央側 ±70 度にいる（逃げ道を塞がれている）。
    // C_IsAtStageEdge と C_TargetOnCenterSide の AND。スコアの状況による倍率は条件を1つしか持てないので、まとめた条件を用意する
    class Cornered : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            constexpr float kCenterSideAngle = 70.0f; // 「中央側」とみなす角度 [度]
            return ctx.bt->blackboard.distanceToArenaEdge <= ctx.param &&
                   ctx.bt->blackboard.targetAngleFromCenter <= kCenterSideAngle;
        }
    };

    // 直近の被弾から param 秒以内。殴られている最中に反撃・離脱を選びやすくするのに使う
    class RecentlyHit : public IBTCondition
    {
    public:
        bool Evaluate(BTContext &ctx) override
        {
            return ctx.bt->blackboard.timeSinceLastHit < ctx.param;
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
        reg.RegisterCondition<TargetStrafing>(C_TargetStrafing);
        reg.RegisterCondition<TargetSlow>(C_TargetSlow);
        reg.RegisterCondition<DamageThresholdExceeded>(C_DamageThresholdExceeded);
        reg.RegisterCondition<IsAtStageEdge>(C_IsAtStageEdge);
        reg.RegisterCondition<TargetOnCenterSide>(C_TargetOnCenterSide);
        reg.RegisterCondition<Cornered>(C_Cornered);
        reg.RegisterCondition<RecentlyHit>(C_RecentlyHit);
    }

} // namespace CCL::AI::BT
