#pragma once

#include "ECS/System/CCL_System.h" // IfSystemの継承元
#include "ECS/Core/CCL_Chunk.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Physics/Collision/CollisionConfigComponent.h"

class ShapeRenderer;

class GroundCheckSystem : public CCL::ECS::IfSystem<GroundCheckSystem,
                              CCL::ECS::Write<TransformComponent>,
                              CCL::ECS::Write<RigidBodyComponent>,
                              CCL::ECS::Read<CollisionConfigComponent>> {
  public:
	GroundCheckSystem() : IfSystem("GroundCheckSystem") {}


	void Update(float dt) override;

};

