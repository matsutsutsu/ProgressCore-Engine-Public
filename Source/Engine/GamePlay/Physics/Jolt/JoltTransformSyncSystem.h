#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"

// ===================================================================================
// エディタ（ImGui）やゲームロジックによって Transform の座標が強制変更された時、
// Joltの物理空間（Staticな地形も含む）をテレポートさせて同期をとるシステム。
// ===================================================================================
class JoltTransformSyncSystem : public CCL::ECS::IfSystem<JoltTransformSyncSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<JoltHandleComponent>> {
public:
    JoltTransformSyncSystem() : IfSystem("JoltTransformSyncSystem") {}
    void Update(float dt) override;
};