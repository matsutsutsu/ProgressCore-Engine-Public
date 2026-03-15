#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Combat/Weapon/Attachments/AttachmentComponents.h"
#include "Game/Logics/Combat/Weapon/Attachments/BulletHitStateComponent.h"
#include "Game/Logics/Combat/Weapon/Core/BulletContextComponent.h"

// 爆発: 情報を読んで爆発エフェクトをスポーンするだけなので全てRead
class OnHitExplosionSystem : public CCL::ECS::IfSystem<OnHitExplosionSystem,
                                 CCL::ECS::Read<TransformComponent>,
                                 CCL::ECS::Read<BulletContextComponent>,
                                 CCL::ECS::Read<BulletHitStateComponent>,
                                 CCL::ECS::Read<OnHitExplosionComponent>> {
  public:
    OnHitExplosionSystem() : IfSystem("OnHitExplosionSystem") {}
    void Update(float dt) override;
};

// 分裂: 情報を読んで子弾をスポーンするだけなので全てRead
class OnHitSplitSystem : public CCL::ECS::IfSystem<OnHitSplitSystem,
                             CCL::ECS::Read<RigidBodyComponent>,
                             CCL::ECS::Read<TransformComponent>,
                             CCL::ECS::Read<BulletContextComponent>,
                             CCL::ECS::Write<BulletHitStateComponent>,
                             CCL::ECS::Read<OnHitSplitComponent>> {
  public:
    OnHitSplitSystem() : IfSystem("OnHitSplitSystem") {}
    void Update(float dt) override;
};

// 反射: 速度の反転、残り反射回数の減少、死亡キャンセルのため全てWrite
class OnHitReflectionSystem : public CCL::ECS::IfSystem<OnHitReflectionSystem,
                                  CCL::ECS::Write<RigidBodyComponent>,
                                  CCL::ECS::Write<BulletHitStateComponent>,
                                  CCL::ECS::Write<OnHitReflectComponent>> {
  public:
    OnHitReflectionSystem() : IfSystem("OnHitReflectionSystem") {}
    void Update(float dt) override;
};

// カオス(反射時ランダム方向): 速度を書き換えるのでRigidBodyのみWrite
class OnHitChaosSystem : public CCL::ECS::IfSystem<OnHitChaosSystem,
                             CCL::ECS::Write<RigidBodyComponent>,
                             CCL::ECS::Read<BulletHitStateComponent>,
                             CCL::ECS::Read<OnHitChaosComponent>> {
  public:
    OnHitChaosSystem() : IfSystem("OnHitChaosSystem") {}
    void Update(float dt) override;
};

// 加速: 速度を書き換えるのでRigidBodyのみWrite
class OnHitAccelerateSystem : public CCL::ECS::IfSystem<OnHitAccelerateSystem,
                                  CCL::ECS::Write<RigidBodyComponent>,
                                  CCL::ECS::Read<BulletHitStateComponent>,
                                  CCL::ECS::Read<OnHitAccelerateComponent>> {
  public:
    OnHitAccelerateSystem() : IfSystem("OnHitAccelerateSystem") {}
    void Update(float dt) override;
};