#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <vector>

// 前方宣言
class AnimatorComponent;
class ModelComponent;
class TimeStateComponent;
class Model;
struct AnimSequence;

// ===================================================================================
// ファイル: AnimationSystem.h
// 概要: アニメーションの「時間進行」と「イベント発火」を担う実働システム
//
// [ 役割 ]
// AnimatorComponent の持つ時間を進め、指定された時間に応じた骨格計算をModelに依頼する。
// また、台本(AnimSequence)に書かれた時間と現在の時間を比較し、
// タイミングが合致したイベント（音、エフェクト、攻撃判定）を EventBus を通じて発行する。
//
// [ Update() の内部構成（Tracyゾーンと対応） ]
//   1. DispatchPointEvents : 点イベント(Sound/Effect/SpawnHitbox)の発火判定
//   2. ComputeAndApplyPose : ブレンド計算・Force Root Lock・骨格ポーズの確定と適用
//   （時間の進行とループ/終了判定は、_world 非依存の純粋関数として .cpp 側に隔離）
// ===================================================================================

class AnimationSystem : public CCL::ECS::IfSystem<AnimationSystem,
                            CCL::ECS::Write<AnimatorComponent>,
                            CCL::ECS::Write<ModelComponent>,
                            CCL::ECS::Read<TimeStateComponent>>
{ // 個別の時間を受け取る
  public:
    AnimationSystem();
    virtual ~AnimationSystem() = default;

    void Update(float dt) override;

    // エディタ用の手動更新関数
    void UpdateManual(CCL::ECS::EntityID entity, float time);

  private:
    // --- Update() を構成する各フェーズ（_world アクセスが必要なもののみメンバ関数化） ---

    /// @brief 台本の点イベント(Sound/Effect/SpawnHitbox)を EventBus 経由で発火する。
    /// @details 区間状態イベント(HitBox/Trail/Blackboard)はここでは発火しない
    ///          （専用システムが区間ポーリングで読む）。
    void DispatchPointEvents(CCL::ECS::EntityID id, AnimatorComponent &animator,
        const AnimSequence *seq, bool movedForward);

    /// @brief ブレンド計算・Force Root Lock を経て、最終的な骨格ポーズを Model へ適用する。
    void ComputeAndApplyPose(CCL::ECS::EntityID id, AnimatorComponent &animator,
        Model *model, const AnimSequence *seq);
};