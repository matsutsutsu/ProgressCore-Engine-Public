/// @file  BTConditions.h
/// @brief どの敵でも使える汎用条件。ボス固有の条件は BTConditions_Boss.h。
///
/// 間合いをIDではなく param で持つ理由:
///   以前は C_RangeClose(15m) / C_RangeMostClose(8m) / C_RangeFar(40m) のように、
///   距離の数値が条件クラスの中に即値で埋まっていた。敵ごとに間合いが違うのは
///   当たり前なので、この形だと「敵の種類 × 間合い」の数だけ ActionID が増え、
///   しかも中間の値（12m など）を作れない。
///   行動側で待機を A_Wait 1つへ畳んだのと、まったく同じ問題への同じ答え。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Actions/BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Actions/BTActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include <cmath>

namespace CCL::AI::BT::CommonConditions
{
/// @brief ターゲットとの距離が param 未満。
struct DistanceLessThan : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.distanceToTarget < ctx.param;
    }
};

/// @brief ターゲットとの距離が param より大きい。
struct DistanceGreaterThan : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.distanceToTarget > ctx.param;
    }
};

/// @brief 蓄積ダメージが閾値を超えた。
struct DamageThresholdExceeded : public IBTCondition
{
    bool Evaluate(BTContext &ctx) override
    {
        return ctx.bt->blackboard.accumulatedDamage >= ctx.bt->blackboard.evadeDamageThreshold;
    }
};

/// @brief 原点から param m 以上離れている（ステージの端に寄っている）。
/// @note  param 未設定(0)のときは従来どおり 48m。ステージ半径が1つしか無かった頃の値。
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
