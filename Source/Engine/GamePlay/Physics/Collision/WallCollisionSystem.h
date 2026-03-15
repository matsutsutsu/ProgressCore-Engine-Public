// Game/Systems/WallCollisionSystem.h
#pragma once
#include "ECS/System/CCL_System.h"
#include "ECS/Core/CCL_Chunk.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Physics/Collision/CollisionConfigComponent.h"


class ShapeRenderer;

class WallCollisionSystem : public CCL::ECS::IfSystem<WallCollisionSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Write<RigidBodyComponent>,
                                CCL::ECS::Read<CollisionConfigComponent>> {
  public:
	WallCollisionSystem() : IfSystem("WallCollisionSystem") {}

	void Update(float dt) override;

};