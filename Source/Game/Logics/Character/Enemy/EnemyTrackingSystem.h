#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "EnemyComponent.h"
#include "../Player/PlayerComponent.h" // パスは適宜合わせてください

// ★修正: Transformは現在位置を読むだけ(Read)にし、移動自体はPhysicsAPIに任せる
class EnemyTrackingSystem : public CCL::ECS::IfSystem<
    EnemyTrackingSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<EnemyComponent>>
{
public:
    EnemyTrackingSystem();
    void Update(float dt) override;
};