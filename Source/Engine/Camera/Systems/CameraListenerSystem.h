#pragma once
// ===========================================================================
// 概要: メインカメラの位置と向きをオーディオのリスナーへ伝える System
//
// 責務:
//   - Camera リソースの視点・注視点・上方向から、リスナーの位置と向きを求める
//   - IAudioAPI::SetListenerPosition へ渡し、3D サウンドの距離減衰と定位に使わせる
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

class CameraListenerSystem : public CCL::ECS::SystemBase
{
public:
    CameraListenerSystem() : SystemBase("CameraListenerSystem") {}
    virtual ~CameraListenerSystem() = default;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override { return {}; }
    void                          Update(float dt) override;
};
