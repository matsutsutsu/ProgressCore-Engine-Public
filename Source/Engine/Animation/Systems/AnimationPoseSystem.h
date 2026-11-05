#pragma once
// ===========================================================================
// 概要: 骨格ポーズを計算して Model に適用する System（R02b_SkeletalPose）
//
// 責務:
//   - 再生中のクリップのポーズを求め、ブレンド中なら前のクリップと混ぜる
//   - ブレンドスペースなら、重みのある点のクリップを重みで混ぜる
//   - ルートボーンを固定（Force Root Lock）し、Model へ渡す
//
// 備考: 時間とイベントは AnimationSystem（ロジック）が進め、ポーズはここ（描画）で求める
//       ロジックは1描画フレームに最大4回走るので、そこで求めると最後の1回以外は捨てられる
//       描画の段なので、一時停止中もポーズは更新される
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

// 実体は struct なので struct で宣言する（class と混ぜると C4099 になる）
struct AnimatorComponent;
struct ModelComponent;
class Model;
struct AnimSequence;

class AnimationPoseSystem : public CCL::ECS::IfSystem<
    AnimationPoseSystem,
    CCL::ECS::Write<AnimatorComponent>,
    CCL::ECS::Write<ModelComponent>>
{
public:
    AnimationPoseSystem(); // 型引数が前方宣言なので、基底の構築は完全な型が見える .cpp で行う
    virtual ~AnimationPoseSystem() = default;

    void Update(float dt) override;

private:
    // ブレンドと Force Root Lock を行い、最終的な骨格ポーズを Model へ適用する
    void ComputeAndApplyPose(CCL::ECS::EntityID id, AnimatorComponent &animator,
                             Model *model, const AnimSequence *seq);

    // 今の側か前の側（previous）の再生元のポーズを作る。今は poseBufferA、前は poseBufferB に書く
    // pinClip は単体のクリップで混ぜる前にルートを固定するか。ブレンドスペースは点ごとに固定する
    // 時間の基準のクリップがモデルになければ false（何も書かない）
    bool ComputeSourcePose(Model *model, AnimatorComponent &animator, bool previous, bool pinClip);
};
