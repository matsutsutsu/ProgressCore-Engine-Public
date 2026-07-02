#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "TimeState.h"

// 座標(Transform)を持っているが、時間(TimeState)を持たないエンティティに
// 自動的に TimeState を付与し、エラーを防ぐフェイルセーフシステム
class TimeScaleSetupSystem : public CCL::ECS::IfSystem<TimeScaleSetupSystem,
    CCL::ECS::Read<TransformComponent>>
{
public:
    TimeScaleSetupSystem() : IfSystem("TimeScaleSetupSystem") 
    {    
        SetExclude<TimeStateComponent>();
    }

    virtual ~TimeScaleSetupSystem() = default;

    void Update(float dt) override;
};