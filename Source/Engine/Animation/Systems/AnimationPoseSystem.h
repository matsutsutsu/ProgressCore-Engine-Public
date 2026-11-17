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
//       今のクリップの時刻は、1ステップ前と今の間を描画の割合（alpha）で補間して使う
//       Transform の補間（TransformInterpolationSystem）と同じ考え方で、ロジックが進まない描画フレームでも骨が動く
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
                             Model *model, const AnimSequence *seq, float alpha);

    // 今の側か前の側（previous）の再生元のポーズを作る。今は poseBufferA、前は poseBufferB に書く
    // pinClip は単体のクリップで混ぜる前にルートを固定するか。ブレンドスペースは点ごとに固定する
    // alpha は今の側の時刻の補間に使う（前の側は補間しない）
    // 時間の基準のクリップがモデルになければ false（何も書かない）
    bool ComputeSourcePose(Model *model, AnimatorComponent &animator, bool previous, bool pinClip, float alpha);

    // 今のクリップの、描画で使う時刻を返す
    // 1ステップ前の時刻（previousFrameTimer）から今の時刻（currentTimer）までを alpha で補間する
    float InterpolateCurrentTimer(const AnimatorComponent &animator, float alpha) const;
};
