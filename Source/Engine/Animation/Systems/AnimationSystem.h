#pragma once
// ===========================================================================
// 概要: アニメーションの時間を進め、点イベントを発火する System（L02_PostUpdate）。
//
// 責務:
//   - AnimatorComponent の再生時刻とブレンドの時刻を Entity ごとの localDt で進める
//   - クリップの点イベント（Sound / Effect / SpawnHitbox）を EventBus で発行する
//   - ループと再生終了の判定
//   - エディタ（シーケンサー）のプレビュー用に、指定時刻のポーズを直接適用する（UpdateManual）
//
// 備考:
//   - 区間のイベント（HitBox / Trail / Particle / Blackboard）は発行せず、各 System が
//     再生時刻を見て区間内かを判定する。
//   - ポーズの計算は AnimationPoseSystem（Render）が行う。時間とイベントは固定ステップの精度が
//     要るのでここ（Logic）に残している。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <vector>

// 実体は struct なので struct で宣言する（class と混ぜると C4099 になる）
struct AnimatorComponent;
struct ModelComponent;
struct TimeStateComponent;
class Model;
struct AnimSequence;

class AnimationSystem : public CCL::ECS::IfSystem<AnimationSystem,
                                                  CCL::ECS::Write<AnimatorComponent>,
                                                  CCL::ECS::Write<ModelComponent>,
                                                  CCL::ECS::Read<TimeStateComponent>>
{
public:
    AnimationSystem();
    virtual ~AnimationSystem() = default;

    void Update(float dt) override;

    // エディタ（AnimationSequencerWindow）用。指定時刻のポーズを計算して Model へ適用する
    void UpdateManual(CCL::ECS::EntityID entity, float time);

private:
    /**
     * @brief 点イベント（Sound / Effect / SpawnHitbox）を EventBus で発行する。
     *
     * 時間の進行やループ判定は m_world を使わないので .cpp の無名名前空間に置いている。
     *
     * @param id           イベントを出すエンティティ。
     * @param animator     イベントの位置（nextEventIndex）を持つ Animator。
     * @param seq          再生中のシーケンス。
     * @param movedForward このフレームで順方向に進んだか。false（逆再生・シーク）なら発火せず、イベントの位置だけ現在時刻に合わせる。
     */
    void DispatchPointEvents(CCL::ECS::EntityID id, AnimatorComponent &animator,
                             const AnimSequence *seq, bool movedForward);
};