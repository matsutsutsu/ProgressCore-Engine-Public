#pragma once
// ===========================================================================
// 概要: 骨格ポーズを計算して Model に適用する System（R02b_SkeletalPose）。
//
// 責務:
//   - 現在のクリップ（ブレンド中なら前のクリップも）のポーズを計算してブレンドする
//   - ブレンドスペースなら、重みのある点のクリップを計算して重みで混ぜる
//   - Force Root Lock（ルートボーンの固定）を掛け、Model へ SetNodePoses する
//
// 備考:
//   - 時間の進行とイベントの発火は AnimationSystem（Logic）、ポーズ計算はここ（Render）に分けている。
//     FixedUpdate は 1 描画フレームに最大 4 回走るが、ポーズを行列にするのは
//     ModelPoseUpdateSystem（R03_Animation）の 1 回だけなので、Logic で計算すると最後以外は捨てられる。
//     さらに「重い → ステップが増える → さらに重い」と悪化する。
//   - Render 側なので、一時停止中（ECSContext::UpdatePaused は UpdateRender だけ呼ぶ）もポーズが更新される。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

// 実体は struct なので struct で宣言する（class と混ぜると C4099 になる）
struct AnimatorComponent;
struct ModelComponent;
class Model;
struct AnimSequence;

class AnimationPoseSystem : public CCL::ECS::IfSystem<AnimationPoseSystem,
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

    /**
     * @brief 今側か前側の再生元（単体のクリップか、ブレンドスペース）のポーズを作る。
     * @param previous 前側（クロスフェードの元）なら true。今側は poseBufferA、前側は poseBufferB に書く。
     * @param pinClip  単体のクリップのとき、作った後にルートを固定するか（混ぜる前の固定）。
     *                 ブレンドスペースは点ごとのルートが違うので、指定によらず点ごとに固定する。
     * @return 時間の基準のクリップがモデルに無ければ false（何も書かない）。
     */
    bool ComputeSourcePose(Model *model, AnimatorComponent &animator, bool previous, bool pinClip);
};
