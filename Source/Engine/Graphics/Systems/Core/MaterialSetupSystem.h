#pragma once
// ===========================================================================
// 概要: ModelComponent を持つのに MaterialComponent がない Entity へ、
//       空の MaterialComponent を付ける System
//
// 責務:
//   - MaterialComponent のないモデルを探し、MaterialComponent を付ける
//
// 備考: ModelRenderSystem は MaterialComponent を必須にしているので、付いていないと描かれない
//       空の MaterialComponent なら、モデルの共有マテリアルでそのまま描かれる
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"

class MaterialSetupSystem : public CCL::ECS::IfSystem<
    MaterialSetupSystem,
    CCL::ECS::Read<ModelComponent>>
{
public:
    MaterialSetupSystem() : IfSystem("MaterialSetupSystem")
    {
        SetExclude<MaterialComponent>();
    }

    void Update(float dt) override;
};
