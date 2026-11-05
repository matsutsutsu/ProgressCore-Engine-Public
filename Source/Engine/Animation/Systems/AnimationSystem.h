#pragma once
// ===========================================================================
// 概要: アニメーションの時間を進め、点イベントを発火する System（L02_PostUpdate）
//
// 責務:
//   - 再生時刻とブレンドの時刻を Entity ごとの localDt で進める
//   - クリップの点イベント（Sound / Effect / SpawnHitbox）を EventBus で出す
//   - ループと再生終了を判定する
//   - シーケンサーのプレビューで、指定時刻のポーズを直接当てる（UpdateManual）
//
// 備考: 区間のイベント（HitBox / Trail など）は出さない。各 System が再生時刻を見て判定する
//       ポーズは AnimationPoseSystem が求める。時間とイベントは固定ステップの精度が要るので、ここに残す
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <vector>

// 実体は struct なので struct で宣言する（class と混ぜると C4099 になる）
struct AnimatorComponent;
struct ModelComponent;
struct TimeStateComponent;
class Model;
struct AnimSequence;

class AnimationSystem : public CCL::ECS::IfSystem<
    AnimationSystem,
    CCL::ECS::Write<AnimatorComponent>,
    CCL::ECS::Write<ModelComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    AnimationSystem(); // 型引数が前方宣言なので、基底の構築は完全な型が見える .cpp で行う
    virtual ~AnimationSystem() = default;

    void Update(float dt) override;

    // エディタ（AnimationSequencerWindow）用。指定時刻のポーズを計算して Model へ適用する
    void UpdateManual(CCL::ECS::EntityID entity, float time);

private:
    // 点イベント（Sound / Effect / SpawnHitbox）のうち、今の時刻までに来たものを EventBus で出す
    // movedForward が false（逆再生・シーク）なら出さず、イベントの位置だけ今の時刻に合わせる
    void DispatchPointEvents(CCL::ECS::EntityID id, AnimatorComponent &animator,
                             const AnimSequence *seq, bool movedForward);
};