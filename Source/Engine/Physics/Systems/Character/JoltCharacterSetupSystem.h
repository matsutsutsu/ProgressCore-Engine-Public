#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"

// プレイヤー専用のコントローラーを生成するファクトリーシステム
class JoltCharacterSetupSystem : public CCL::ECS::IfSystem<JoltCharacterSetupSystem,
                                     CCL::ECS::Read<TransformComponent>,
                                     CCL::ECS::Read<JoltCharacterConfigComponent>> {
  public:
    JoltCharacterSetupSystem() : IfSystem("JoltCharacterSetupSystem") {}
    virtual ~JoltCharacterSetupSystem() = default;

    void Update(float dt) override;
};