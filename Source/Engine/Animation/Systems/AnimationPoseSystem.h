#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"

// 前方宣言（実体は struct なので struct で宣言する。class と混ぜると C4099 になる）
struct AnimatorComponent;
struct ModelComponent;
class Model;
struct AnimSequence;

// ===================================================================================
// ファイル: AnimationPoseSystem.h
// 概要: 骨格ポーズの確定と Model への適用だけを担う描画側システム。
//
// [ なぜ AnimationSystem から分離したか ]
// FixedUpdate は1描画フレームに最大4回まわる（Framework::MAX_STEPS / accumulator）。
// 一方、確定したポーズを行列へ変換するのは Render グループの ModelPoseUpdateSystem
// （R03_Animation）ただ1つで、SetNodePoses が書くのは node の position/rotation/scale
// までなので、最後の固定ステップ以外のポーズは行列になる前に上書きされて消える。
// つまり FixedUpdate に置くとキーフレーム評価が丸ごと2〜4倍の捨て仕事になり、
// しかも「重い→ステップが増える→さらに重い」という自己増幅ループを作る。
//
// そこで
//   ・時間進行 / イベント発火（固定ステップ精度が要る）→ AnimationSystem（Logic）
//   ・ポーズ計算（描画にしか使わない）                 → 本システム（Render）
// と責務を割り、ポーズ計算は1描画フレームにつき必ず1回だけ実行する。
//
// 副次効果として、一時停止中（World::UpdatePaused は UpdateRender しか呼ばない）でも
// ポーズが正しく更新されるようになる。
// ===================================================================================

class AnimationPoseSystem : public CCL::ECS::IfSystem<AnimationPoseSystem,
                                CCL::ECS::Write<AnimatorComponent>,
                                CCL::ECS::Write<ModelComponent>>
{
  public:
    AnimationPoseSystem();
    virtual ~AnimationPoseSystem() = default;

    void Update(float dt) override;

  private:
    // ブレンド計算・Force Root Lock を経て、最終的な骨格ポーズを Model へ適用する。
    void ComputeAndApplyPose(CCL::ECS::EntityID id, AnimatorComponent &animator,
        Model *model, const AnimSequence *seq);
};
