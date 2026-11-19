#pragma once
// ===========================================================================
// 概要: 骨格ポーズを計算して Model に適用する System（R02b_SkeletalPose）
//
// 責務:
//   - 再生中のクリップのポーズを求め、ブレンド中なら前のクリップと混ぜる
//   - ブレンドスペースなら、重みのある点のクリップを重みで混ぜる
//   - ルートボーンを固定（Force Root Lock）し、Model へ渡す
//   - シーケンサーのプレビューで、指定時刻のポーズを直接当てる（ApplyPoseAt）
//
// 備考: 時間とイベントは AnimationSystem（ロジック）が進め、ポーズはここ（描画）で求める
//       ロジックは1描画フレームに最大4回走るので、そこで求めると最後の1回以外は捨てられる
//       描画の段なので、一時停止中もポーズは更新される
//       今のクリップの時刻は、1ステップ前と今の間を描画の割合（alpha）で補間して使う
//       Transform の補間（TransformInterpolationSystem）と同じ考え方で、ロジックが進まない描画フレームでも骨が動く
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Resource/Model.h"
#include <vector>

// 実体は struct なので struct で宣言する（class と混ぜると C4099 になる）
struct AnimatorComponent;
struct ModelComponent;
struct AnimSequence;

class AnimationPoseSystem : public CCL::ECS::IfSystem<
    AnimationPoseSystem,
    CCL::ECS::Read<AnimatorComponent>, // 再生の状態は読むだけ（進めるのは AnimationSystem）
    CCL::ECS::Write<ModelComponent>>
{
public:
    AnimationPoseSystem(); // 型引数が前方宣言なので、基底の構築は完全な型が見える .cpp で行う
    virtual ~AnimationPoseSystem() = default;

    void Update(float dt) override;

    // エディタ（シーケンサー）用。今のクリップの time（秒）のポーズを求めて Model へ当てる
    // ブレンドは見ず、Force Root Lock は RootMotionComponent の有無に関係なく掛ける
    void ApplyPoseAt(CCL::ECS::EntityID entity, float time);

private:
    // ブレンドと Force Root Lock を行い、最終的な骨格ポーズを Model へ適用する
    void ComputeAndApplyPose(CCL::ECS::EntityID id, const AnimatorComponent &animator,
                             Model *model, const AnimSequence *seq, float alpha);

    // 今の側か前の側（previous）の再生元のポーズを作る。今は m_currentPose、前は m_previousPose に書く
    // pinClip は単体のクリップで混ぜる前にルートを固定するか。ブレンドスペースは点ごとに固定する
    // alpha は今の側の時刻の補間に使う（前の側は補間しない）
    // 時間の基準のクリップがモデルになければ false（何も書かない）
    bool ComputeSourcePose(Model *model, const AnimatorComponent &animator, bool previous, bool pinClip, float alpha);

    // 今のクリップの、描画で使う時刻を返す
    // 1ステップ前の時刻（previousFrameTimer）から今の時刻（currentTimer）までを alpha で補間する
    float InterpolateCurrentTimer(const AnimatorComponent &animator, float alpha) const;

    // --- ポーズ計算の作業用（全 Entity で使い回し、毎フレームの確保を避ける。直列に回すので共有してよい）---
    std::vector<Model::NodePose> m_currentPose;  // 今のクリップ（ブレンド後の結果もここ）
    std::vector<Model::NodePose> m_previousPose; // クロスフェード中の前のクリップ
    std::vector<Model::NodePose> m_samplePose;   // ブレンドスペースで、点 1 本分を計算する作業用
};
