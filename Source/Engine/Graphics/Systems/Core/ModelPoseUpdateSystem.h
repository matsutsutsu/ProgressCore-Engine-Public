#pragma once
// ===========================================================================
// 概要: モデルの各ノードの行列を、描画の前に確定させる System（R03_Animation）。
//
// 責務:
//   - 各ノードの姿勢（AnimationPoseSystem が書いたもの）から、グローバル行列と
//     ワールド行列を求める
//
// 備考: 基準は補間済みの renderWorldMatrix。補間（R00）の後、AABB 更新（R04）と
//       描画（R08）の前に走る。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"

class ModelPoseUpdateSystem : public CCL::ECS::IfSystem<ModelPoseUpdateSystem,
                                                        CCL::ECS::Read<TransformComponent>,
                                                        CCL::ECS::Write<ModelComponent>>
{
public:
    ModelPoseUpdateSystem() : IfSystem("ModelPoseUpdateSystem") {}

    void Update(float dt) override;
};
