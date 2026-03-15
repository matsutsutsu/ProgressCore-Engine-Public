#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/Collision/SphereColliderComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Combat/Weapon/Core/BulletContextComponent.h"
#include "Game/Logics/Combat/Weapon/Attachments/BulletHitStateComponent.h"
#include <DirectXMath.h>

// ===================================================================================
// クラス名: BulletCollisionSystem
// ===================================================================================
class BulletCollisionSystem : public CCL::ECS::IfSystem<BulletCollisionSystem,
                                  CCL::ECS::Write<BulletContextComponent>,
                                  CCL::ECS::Write<RigidBodyComponent>,
                                  CCL::ECS::Write<SphereColliderComponent>,
                                  CCL::ECS::Write<BulletHitStateComponent>> {
  public:
    BulletCollisionSystem() : IfSystem("BulletCollisionSystem") {}

    void Update(float dt) override;

};