#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"

// 前方宣言
class AnimatorComponent;
class ModelComponent;
class TimeStateComponent;

// ===================================================================================
// ファイル: AnimationSystem.h
// 概要: アニメーションの「時間進行」と「イベント発火」を担う実働システム
// 
// [ 役割 ]
// AnimatorComponent の持つ時間を進め、指定された時間に応じた骨格計算をModelに依頼する。
// また、台本(AnimSequence)に書かれた時間と現在の時間を比較し、
// タイミングが合致したイベント（音、エフェクト、攻撃判定）を EventBus を通じて発行する。
// ===================================================================================


class AnimationSystem : public CCL::ECS::IfSystem<AnimationSystem,
                            CCL::ECS::Write<AnimatorComponent>,
                            CCL::ECS::Write<ModelComponent>,
                            CCL::ECS::Read<TimeStateComponent>> { // 個別の時間を受け取る
  public:
    AnimationSystem();
    virtual ~AnimationSystem() = default;

    void Update(float dt) override;

    // エディタ用の手動更新関数
    void UpdateManual(CCL::ECS::EntityID entity, float time);
};