#pragma once
#include "ECS/System/CCL_System.h"

// ImGui経由で音イベントを発行するデバッグシステム
class AISoundDebuggerSystem : public CCL::ECS::IfSystem<AISoundDebuggerSystem> {
public:
    AISoundDebuggerSystem() : IfSystem("AISoundDebuggerSystem") {}
    virtual ~AISoundDebuggerSystem() = default;

    void Update(float dt) override;
};