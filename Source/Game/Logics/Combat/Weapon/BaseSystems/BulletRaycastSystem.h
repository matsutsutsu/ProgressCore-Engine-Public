#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Combat/Weapon/Core/BulletContextComponent.h"

// ===================================================================================
// クラス名: BulletRaycastSystem
// 役割: 弾丸の「環境干渉（反射など）」を担当する物理予測システム
//
// 目的:
//   - 高速で移動する弾丸が壁をすり抜けるのを防ぐ (CCD: Continuous Collision Detection)
//   - 「反射(Reflection)」アタッチメントがついている弾の軌道を変更する
// ===================================================================================


// 反射など、弾丸の物理的な環境干渉を担当するシステム
class BulletRaycastSystem : public CCL::ECS::IfSystem<BulletRaycastSystem,
                                CCL::ECS::Read<BulletContextComponent>,
                                CCL::ECS::Read<RigidBodyComponent>,
                                CCL::ECS::Write<TransformComponent>> {
  public:
    BulletRaycastSystem() : IfSystem("BulletRaycastSystem") {}

    void Update(float dt) override;
};