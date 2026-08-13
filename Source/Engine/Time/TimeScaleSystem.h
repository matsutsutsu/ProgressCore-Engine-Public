#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "TimeState.h"

class TimeScaleSystem : public CCL::ECS::IfSystem<TimeScaleSystem, CCL::ECS::Write<TimeStateComponent>>
{
public:
    TimeScaleSystem();

    void Initialize() override;

    // 実際の時間の計算処理
    void Update(float rawDt) override;

    void OnGui() override;
};
