#pragma once
// ===========================================================================
// 概要: 骨格ポーズの後処理の共通関数。
//
// 責務:
//   - ルートボーンを基準姿勢へ固定する（Force Root Lock）
//
// 備考: 実行時（AnimationPoseSystem）とエディタのプレビュー（AnimationSystem::UpdateManual）の
//       両方から使うのでヘッダに置いている。
// ===========================================================================
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include <vector>

namespace CCL::Anim
{
    /**
     * @brief ルートボーンの XZ（lockVertical なら Y も）を基準姿勢へ固定する。
     * @note XZ はクリップ自身の 0 秒目を基準にする（前進量を消すのが目的なので、クリップごとの基準でよい）。
     *       Y は ModelResource のバインドポーズを基準にする。クリップの 0 秒目の Y を基準にすると、
     *       その姿勢が膝を曲げている場合にアニメ全体がその分浮く・沈むため。
     */
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
