#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"

// プレイヤー専用のコントローラーを生成するファクトリーシステム
class JoltCharacterSetupSystem : public CCL::ECS::IfSystem<JoltCharacterSetupSystem,
                                                           CCL::ECS::Read<TransformComponent>,
                                                           CCL::ECS::Read<JoltCharacterConfigComponent>>
{
public:
    JoltCharacterSetupSystem() : IfSystem("JoltCharacterSetupSystem")
    {
        SetExclude<JoltCharacterHandleComponent>();
    }
    virtual ~JoltCharacterSetupSystem() = default;

    void Update(float dt) override;
};