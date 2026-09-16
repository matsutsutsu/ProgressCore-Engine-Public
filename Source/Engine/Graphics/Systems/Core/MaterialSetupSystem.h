#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"

// モデルのノード行列やボーン行列を描画前に確定させるシステム
class MaterialSetupSystem : public CCL::ECS::IfSystem<MaterialSetupSystem,
                                                      CCL::ECS::Read<ModelComponent>>
{
public:
    MaterialSetupSystem() : IfSystem("MaterialSetupSystem")
    {
        SetExclude<MaterialComponent>();
    }

    void Update(float dt) override;
};
