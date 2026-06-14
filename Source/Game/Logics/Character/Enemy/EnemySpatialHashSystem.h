#pragma once
// ===========================================================================
// Systems/EnemySpatialHashSystem.h
// ---------------------------------
// @file
// @brief 毎フレーム BulletSpatialHash を敵位置で再構築するSystem（L01）。
//        弾×敵判定の「Build フェーズ」。弾判定（L06）より必ず先に走ること。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"

/// @brief 「これは敵である」ことを示す空タグ（EnemySystem 側で既存なら流用してよい）。
struct EnemyTag {};

class EnemySpatialHashSystem
    : public CCL::ECS::IfSystem<EnemySpatialHashSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<EnemyTag>,
    CCL::ECS::Read<JoltSphereColliderComponent>>
{
public:
    EnemySpatialHashSystem() : IfSystem("EnemySpatialHashSystem") {}
    void Update(float dt) override;
};