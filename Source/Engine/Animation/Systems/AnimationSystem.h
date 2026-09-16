#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <vector>

// 前方宣言（実体は struct なので struct で宣言する。class と混ぜると C4099 になる）
struct AnimatorComponent;
struct ModelComponent;
struct TimeStateComponent;
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
//   （時間の進行とループ/終了判定は、m_world 非依存の純粋関数として .cpp 側に隔離）
//
// [ ポーズ計算はここには無い ]
// 骨格ポーズの確定と適用は AnimationPoseSystem（Render / R02b_SkeletalPose）が担う。
// 本システムは FixedUpdate 側で1描画フレームに最大4回まわる（Framework::MAX_STEPS）が、
// ポーズを消費するのは描画1回きりなので、ここで計算すると2〜4倍の捨て仕事になる。
// 時間進行とイベント発火だけは固定ステップ精度が必要なので、ここに残している。
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
    // --- Update() を構成する各フェーズ（m_world アクセスが必要なもののみメンバ関数化） ---

    // 台本の点イベント(Sound/Effect/SpawnHitbox)を EventBus 経由で発火する。
    //
    // 区間状態イベント(HitBox/Trail/Blackboard)はここでは発火しない
    // （専用システムが区間ポーリングで読む）。
    void DispatchPointEvents(CCL::ECS::EntityID id, AnimatorComponent &animator,
                             const AnimSequence *seq, bool movedForward);
};