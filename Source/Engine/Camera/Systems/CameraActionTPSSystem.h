#pragma once
// ハイスピードアクションTPSカメラの挙動を毎フレーム計算する独立システム。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Camera/Components/VirtualCameraComponents.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

class CameraActionTPSSystem : public CCL::ECS::IfSystem<
                                  CameraActionTPSSystem,
                                  CCL::ECS::Write<VirtualCamera>,
                                  CCL::ECS::Write<CameraBodyTPSComponent>, // 内部状態(Pitch/Yaw等)を更新するためWrite
                                  CCL::ECS::Write<TransformComponent>>  // カメラ自身のTransformを確定させるためWrite
{
  public:
    CameraActionTPSSystem()
        : IfSystem("CameraActionTPSSystem")
    {
    }
    virtual ~CameraActionTPSSystem() = default;

    void Initialize() override;
    void Update(float dt) override;
};