#include "ModelPoseUpdateSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void ModelPoseUpdateSystem::Update(float dt)
{
    // 全エンティティのモデルポーズを並列更新
    ForEachParallel([&](const TransformComponent &trans, ModelComponent &model) {
        auto *modelPtr = model.GetModel();
        if (!modelPtr) return;

        // モデル内部のノード階層に行列を流し込む
        // (アニメーションが有効な場合は、ここでボーンの計算も行う)
        // worldMatrix を渡すことで、モデル内の各メッシュがどこに居るべきか確定させる
        modelPtr->UpdateTransform(trans.renderWorldMatrix);
    });
}

// Logic(L02) から Render に移動。補間(R00)の後・BoundsUpdate(R04)/描画(R08)の前。
//    R03_Animation がスケルタル更新の枠なので、その直前の R02〜R03 帯に置く。
REGISTER_RENDER_SYSTEM(ModelPoseUpdateSystem, Priority::RenderStage::R03_Animation);