#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "JoltCharacterHandleComponent.h"

class JoltCharacterUpdateSystem
    : public CCL::ECS::IfSystem<JoltCharacterUpdateSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Read<JoltCharacterHandleComponent>> {
public:
  JoltCharacterUpdateSystem() : IfSystem("JoltCharacterUpdateSystem") {}
  virtual ~JoltCharacterUpdateSystem() = default;

  void Update(float dt) override;
};