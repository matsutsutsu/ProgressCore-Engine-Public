#pragma once
// ===========================================================================
// Systems/BulletCollisionSystem.h
// -------------------------------
// @file
// @brief 弾の「前フレーム位置→現在位置」の線分をRayCastし、ヒットしたら OnHitフェーズを
//        実行するSystem（L06_Resolution）。線分掃引なので高速弾の壁抜けが起きない。
//
// @note RayCast は JoltPhysicsManager の NarrowPhaseQuery を直接使用。
//       ダメージ適用・弾破棄を行うため直列実行(ForEachWithID)。並列化しない。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Wand/Bullet/BulletComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

/**
 * @brief 弾の衝突判定とOnHitフェーズ処理を行うSystem。
 */
class BulletCollisionSystem
    : public CCL::ECS::IfSystem<BulletCollisionSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<BulletStateComponent>,
    CCL::ECS::Read<BulletProgramRef>,
    CCL::ECS::Read<BulletTag>>
{
public:
    BulletCollisionSystem() : IfSystem("BulletCollisionSystem") {}
    void Update(float dt) override;
};