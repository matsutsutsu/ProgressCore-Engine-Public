#pragma once
// ===========================================================================
// Systems/BulletLifetimeSystem.h
// ------------------------------
// @file
// @brief 寿命(age>=lifetime)を超えた弾を破棄するSystem（直列・L07_Cleanup）。
//
// @note 構造変更(破棄=共有PendingOps)を伴うため直列実行(ForEachWithID)。並列化しない。
// @warning Step 6 で本Systemは「破棄」から「継承(=新弾の再射出)」へ進化させる予定。
//          現状は単純破棄でリークを防ぐ役割。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h" // ※パスに合わせる
#include "Game/Logics/Wand/Data/BulletComponents.h"

/**
 * @brief 寿命切れの弾を回収するSystem。
 */
class BulletLifetimeSystem
    : public CCL::ECS::IfSystem<BulletLifetimeSystem,
    CCL::ECS::Read<BulletStateComponent>,
    CCL::ECS::Read<BulletTag>>
{
public:
    BulletLifetimeSystem() : IfSystem("BulletLifetimeSystem") {}

    void Update(float dt) override;
};