#pragma once
// ===========================================================================
// Systems/BulletWallCollisionSystem.h
// ------------------------------------
// @file
// @brief 弾×壁 判定を DungeonGrid::RaycastDDA で行うSystem（L06、並列OK）。
//
// 壁に当たったとき:
//   ・Reflection コマンドがあれば反射（dir = reflect(dir, normal)）
//   ・なければ hitWall フラグを立て L07 で破棄（直接 Destroy しない→並列安全）
//
// @note 弾の破棄は BulletInheritanceSystem(L07) が hitWall を見て行う。
//       ここでは構造変更を一切しない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Wand/Bullet/BulletComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"

class BulletWallCollisionSystem
    : public CCL::ECS::IfSystem<BulletWallCollisionSystem,
    CCL::ECS::Write<BulletStateComponent>,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<BulletProgramRef>,
    CCL::ECS::Read<BulletTag>>
{
public:
    BulletWallCollisionSystem() : IfSystem("BulletWallCollisionSystem") {}
    void Update(float dt) override;
};