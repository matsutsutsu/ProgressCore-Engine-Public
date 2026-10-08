#include "ModelPoseUpdateSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void ModelPoseUpdateSystem::Update(float dt)
{
    ForEachParallel([&](const TransformComponent &trans, ModelComponent &model) {
        auto *modelPtr = model.GetModel();
        if (!modelPtr) return;

        // 各ノードの姿勢から、ノードのグローバル行列とワールド行列を求める
        modelPtr->UpdateTransform(trans.renderWorldMatrix);
    });
}

// 補間（R00）と骨格ポーズ（R02b）の後、AABB 更新（R04）と描画（R08）の前
REGISTER_RENDER_SYSTEM(ModelPoseUpdateSystem, Priority::RenderStage::R03_Animation);