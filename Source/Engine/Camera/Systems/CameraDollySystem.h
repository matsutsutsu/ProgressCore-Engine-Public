/// @file  CameraDollySystem.h
/// @brief スプライン軌道上のカメラ（台車）を移動させ、VirtualCamera に座標を書き込むシステム。

#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Camera/Components/CameraDollyCartComponent.h"
// ※TimeStateComponentの想定パス
#include "Engine/Timer/TimeState.h"

// ECS指示書に基づき、名前空間でクラスを囲まない
class CameraDollySystem : public CCL::ECS::IfSystem<
                              CameraDollySystem,
                              CCL::ECS::Write<VirtualCamera>,
                              CCL::ECS::Write<CameraDollyCartComponent>,
                              CCL::ECS::Read<TimeStateComponent>> // 時間管理厳守: TimeScaleを読む
{
  public:
    CameraDollySystem()
        : IfSystem("CameraDollySystem")
    {
    }
    virtual ~CameraDollySystem() = default;

    void Update(float dt) override;
};