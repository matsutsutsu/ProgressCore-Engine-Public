#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Combat/Weapon/Attachments/AttachmentComponents.h"
#include "Game/Logics/Combat/Weapon/Core/BulletContextComponent.h"

// ホーミング: 速度と自身の設定(ターゲットID等)を更新するのでWrite
class TravelHomingSystem : public CCL::ECS::IfSystem<TravelHomingSystem,
                               CCL::ECS::Write<RigidBodyComponent>,
                               CCL::ECS::Read<TransformComponent>,
                               CCL::ECS::Write<TravelHomingComponent>> {
  public:
    TravelHomingSystem() : IfSystem("TravelHomingSystem") {}
    void Update(float dt) override;
};

// 加速: 速度を書き換える
class TravelAccelerateSystem : public CCL::ECS::IfSystem<TravelAccelerateSystem,
                                   CCL::ECS::Write<RigidBodyComponent>,
                                   CCL::ECS::Read<TravelAccelerateComponent>> {
  public:
    TravelAccelerateSystem() : IfSystem("TravelAccelerateSystem") {}
    void Update(float dt) override;
};

// 減速: 速度を書き換える
class TravelSlowSystem : public CCL::ECS::IfSystem<TravelSlowSystem,
                             CCL::ECS::Write<RigidBodyComponent>,
                             CCL::ECS::Read<TravelSlowComponent>> {
  public:
    TravelSlowSystem() : IfSystem("TravelSlowSystem") {}
    void Update(float dt) override;
};

// 爆発(時限): タイマーを減らすのでTravelExplosionはWrite
class TravelExplosionSystem : public CCL::ECS::IfSystem<TravelExplosionSystem,
                                  CCL::ECS::Read<TransformComponent>,
                                  CCL::ECS::Read<BulletContextComponent>,
                                  CCL::ECS::Write<TravelExplosionComponent>> {
  public:
    TravelExplosionSystem() : IfSystem("TravelExplosionSystem") {}
    void Update(float dt) override;
};

// 分裂(時限): タイマー減算と親弾の軌道変更があるのでWrite多め
class TravelSplitSystem : public CCL::ECS::IfSystem<TravelSplitSystem,
                              CCL::ECS::Write<RigidBodyComponent>,
                              CCL::ECS::Read<TransformComponent>,
                              CCL::ECS::Read<BulletContextComponent>,
                              CCL::ECS::Write<TravelSplitComponent>> {
  public:
    TravelSplitSystem() : IfSystem("TravelSplitSystem") {}
    void Update(float dt) override;
};

// カオス: タイマー減算と速度変更があるので両方Write
class TravelChaosSystem : public CCL::ECS::IfSystem<TravelChaosSystem,
                              CCL::ECS::Write<RigidBodyComponent>,
                              CCL::ECS::Write<TravelChaosComponent>> {
  public:
    TravelChaosSystem() : IfSystem("TravelChaosSystem") {}
    void Update(float dt) override;
};