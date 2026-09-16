#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"

// JoltのContactListenerに溜まった「衝突の手紙」を回収し、
// ECSのEventBusに一斉送信する配達員（システム）
class JoltCollisionEventSystem
    : public CCL::ECS::IfSystem<JoltCollisionEventSystem>
{
public:
    JoltCollisionEventSystem() : IfSystem("JoltCollisionEventSystem") {}
    void Update(float dt) override;
};