#pragma once
// ===========================================================================
// 概要: アニメーションの時間を進め、点イベントを発火する System（L02_PostUpdate）
//
// 責務:
//   - 再生時刻とブレンドの時刻を Entity ごとの localDt で進める
//   - クリップの点イベント（Sound / Effect / SpawnHitbox）を EventBus で出す
//   - ループと再生終了を判定する
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
struct AnimSequence;

class AnimationSystem : public CCL::ECS::IfSystem<
    AnimationSystem,
    CCL::ECS::Write<AnimatorComponent>,
    CCL::ECS::Read<ModelComponent>, // Model を持つキャラだけに絞る（中身は読まない）
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    AnimationSystem(); // 型引数が前方宣言なので、基底の構築は完全な型が見える .cpp で行う
    virtual ~AnimationSystem() = default;

    void Update(float dt) override;

private:
    // 点イベント（Sound / Effect / SpawnHitbox）のうち、今の時刻までに来たものを EventBus で出す
    // movedForward が false（逆再生・シーク）なら出さず、イベントの位置だけ今の時刻に合わせる
    void DispatchPointEvents(CCL::ECS::EntityID id, AnimatorComponent &animator,
                             const AnimSequence *seq, bool movedForward);

    // currentTimer（ブレンド中なら previousTimer も）を進める。順方向に進んだら true
    static bool AdvanceTimers(AnimatorComponent &animator, const AnimSequence *seq, float localDt);

    // 実効の再生速度（ステートの速度 × 足滑り補正 × 速度カーブ）を返す
    static float ComputeEffectiveSpeed(const AnimatorComponent &animator, const AnimSequence *seq);

    // duration の境界でループ・終了を判定し、currentTimer をクリップ内に収める
    static void ResolveLoopAndFinish(AnimatorComponent &animator, const AnimSequence *seq);

    // 効果音のピッチに、±range の揺らぎを足して返す
    static float RandomizePitch(float pitch, float range);
};
