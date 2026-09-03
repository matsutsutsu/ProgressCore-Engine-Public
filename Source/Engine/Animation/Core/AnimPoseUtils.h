#pragma once
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Resource/ModelResource.h"
#include <vector>

// ===================================================================================
// ファイル: AnimPoseUtils.h
// 概要: 骨格ポーズの後処理のうち、実行時(AnimationPoseSystem)とエディタプレビュー
//       (AnimationSystem::UpdateManual)の両方から使う共通ヘルパー。
//
// 以前は AnimationSystem.cpp の匿名 namespace に置いていたが、ポーズ計算を
// Render グループ(AnimationPoseSystem)へ分離した際に呼び出し元が2つの .cpp へ
// 分かれたため、ここへ引き上げて実装の重複を避ける。
// ===================================================================================

namespace CCL::Anim
{
// ルートボーンのXZ（と条件付きでY）を基準姿勢へピン留めする共通ヘルパー。
//
// XZは常に「対象クリップ自身の0秒目」を基準にする（前進量を殺す目的なので
// クリップごとの基準で正しい）。Yは lockVertical が true のときだけ、
// ModelResource のバインドポーズ（実行中不変の基準姿勢）を基準にする。
// クリップ自身の0秒目のYを基準にすると、そこが既に膝を曲げた姿勢の場合
// アニメ全体がその分浮く/沈む不具合になるため（過去に実際に発生した不具合）。
inline void PinRootMotionNode(Model *model, int animIndex, int rootNodeIndex,
    std::vector<Model::NodePose> &poseBuffer, bool lockVertical)
{
    if (!model || rootNodeIndex < 0 || rootNodeIndex >= (int)poseBuffer.size())
        return;

    // 0秒ポーズ（前進量を殺すためのXZ基準）はクリップ不変。キャッシュから取得して毎フレーム再計算を避ける。
    const Model::NodePose &initPose = model->GetPinnedPoseAtZero(animIndex, rootNodeIndex);
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
