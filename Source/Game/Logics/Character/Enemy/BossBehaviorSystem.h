#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Character/Enemy/BossComponent.h"
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"

#include <SimpleMath.h>

class ShapeRenderer;

class BossBehaviorSystem : public CCL::ECS::IfSystem<BossBehaviorSystem,
                               CCL::ECS::Write<BossComponent>,
                               CCL::ECS::Write<TransformComponent>,
                               CCL::ECS::Write<RigidBodyComponent>, 
                               CCL::ECS::Write<AnimatorComponent>,
                               CCL::ECS::Read<ModelComponent>> {
  public:
    using Vector3 = DirectX::SimpleMath::Vector3;

    BossBehaviorSystem() : IfSystem("BossBehaviorSystem") {}

    void Update(float dt) override;
    void OnDrawDebug(ShapeRenderer *renderer) override;

  private:
    // 行動決定AI
    void DecideNextAction(BossComponent &boss, float distToPlayer);

   // 引数に AnimatorComponent を追加
    void UpdateMove(float dt, BossComponent& boss, TransformComponent& trans, RigidBodyComponent& rb, const Vector3& playerPos, AnimatorComponent& anim);
    void UpdateCharge(float dt, BossComponent& boss, TransformComponent& trans, RigidBodyComponent& rb, const Vector3& dirToPlayer, AnimatorComponent& anim);
    void UpdateSweep(float dt, BossComponent& boss, TransformComponent& trans, const Vector3& playerPos, AnimatorComponent& anim);
    void UpdateSummon(float dt, BossComponent& boss, TransformComponent& trans, AnimatorComponent& anim);
    //void UpdateShockwave(float dt, BossComponent& boss, TransformComponent& trans, const Vector3& playerPos, AnimatorComponent& anim);

    void SpawnMinions(const Vector3& centerPos);

    // ★アニメーションIDキャッシュ
    int _animIdMove   = -1;
    int _animIdSlash  = -1;
    int _animIdTackle = -1;
    int _animIdSummon = -1;
};