#pragma once
// ===========================================================================
// Systems/BulletTravelSystem.h
// ----------------------------
// @file
// @brief 飛行中の弾を毎フレーム更新するSystem（Travelフェーズ）。
//        Travel命令(現状 Accelerate=毎フレーム加速)を適用し、direction×speed×localDt で移動、
//        age を加算する。★弾の破棄は行わない（並列ループ内で構造変更しないため）。
//
// 実行ステージ: L02_Update（並列化の主戦場）。
// @note Registry は読み取り専用アクセス。per-entity 書き込み(Transform/BulletState)のみ行うため
//       ForEachParallel が安全。寿命切れの破棄は BulletLifetimeSystem(L07) が担当する。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"            // ※パスは環境に合わせる
#include "../Data/BulletComponents.h"
#include "Engine/Transform/Components/TransformComponent.h" // ※パスに合わせる
#include "Engine/Timer/TimeState.h" // ※パスに合わせる

/**
 * @brief 飛行中の弾の移動・速度更新・寿命カウントを行うSystem。
 */
class BulletTravelSystem
    : public CCL::ECS::IfSystem<BulletTravelSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<BulletStateComponent>,
    CCL::ECS::Read<BulletProgramRef>,
    CCL::ECS::Read<BulletTag>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    BulletTravelSystem() : IfSystem("BulletTravelSystem") {}

    /**
     * @brief 全弾の Travel命令適用 → 移動 → age 加算。
     * @param dt 未使用（per-entity の TimeStateComponent.localDt を使うため）。
     */
    void Update(float dt) override;
};