#pragma once
// 骨格ポーズの後処理の共通関数
// 実行時（AnimationPoseSystem）とエディタのプレビュー（AnimationSystem::UpdateManual）の両方が使う
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include <vector>

namespace CCL::Anim
{
    // ルートボーンの XZ（lockVertical なら Y も）を基準の姿勢へ固定する
    // XZ はクリップの0秒目を基準にする。Y はバインドポーズを基準にする（0秒目が膝を曲げていると、アニメ全体が浮き沈みする）
    inline void PinRootMotionNode(Model *model, int animIndex, int rootNodeIndex,
                                  std::vector<Model::NodePose> &poseBuffer, bool lockVertical)
    {
        if (!model || rootNodeIndex < 0 || rootNodeIndex >= (int)poseBuffer.size())
            return;

        // 0 秒目のポーズはクリップごとに変わらないので、キャッシュから取得する
        const Model::NodePose &initPose      = model->GetPinnedPoseAtZero(animIndex, rootNodeIndex);
        poseBuffer[rootNodeIndex].position.x = initPose.position.x;
        poseBuffer[rootNodeIndex].position.z = initPose.position.z;

        if (lockVertical)
        {
            const ModelResource *modelRes = model->GetResource();
            if (modelRes && rootNodeIndex < (int)modelRes->GetNodes().size())
                poseBuffer[rootNodeIndex].position.y = modelRes->GetNodes()[rootNodeIndex].translate.y;
        }
    }
} // namespace CCL::Anim
