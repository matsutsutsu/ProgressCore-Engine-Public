#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"

class ProtoDebugUISystem : public CCL::ECS::IfSystem<ProtoDebugUISystem>
{
  public:
    ProtoDebugUISystem()
        : IfSystem("ProtoDebugUISystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;
    void OnGui() override; // ★追加: エディタ/ダッシュボード描画用コールバック
};