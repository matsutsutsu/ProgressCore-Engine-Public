#pragma once
// ===========================================================================
// 概要: トレイルの持ち主（Animator を持つ祖先）を見つけ、アニメーションの Trail 区間
//       イベントに合わせて TrailComponent の emitting を切り替える System（L05_Collision）。
//
// 責務:
//   - すべてのトレイルについて、Animator を持つ祖先を探して ownerEntity に持つ
//   - 持ち主が消えたら ownerEntity を解き、次のフレームで探し直す
//   - animControlled なトレイルは、持ち主の今の再生時刻が Trail 区間に入っているかで emitting を決める
//
// 備考: 走査するのは Animator 側ではなくトレイル側（CombatAnimationSyncSystem と同じ形）。
//       書き込むのは自分の TrailComponent だけで、祖先の Animator は GetComponent で読むだけ。
//       武器は BoneAttachment などでキャラの子孫に付いているので、親をたどって最初に
//       見つかった Animator の持ち主を owner にする（インスペクタで owner を指定しなくてよい）。
//       animControlled でないトレイルも owner を探すのは、TrailRecordSystem が一時トレイルの
//       時計に owner の TimeStateComponent を使うため。
//       AnimationSystem（L02_PostUpdate）が再生時刻を進めた後、TrailRecordSystem（L06）と
//       描画側の TrailSystem（R08）が読む前に走るので、同じフレームで揃う。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"

struct AnimatorComponent;

class TrailAnimationSyncSystem
    : public CCL::ECS::IfSystem<TrailAnimationSyncSystem,
                                CCL::ECS::Read<TransformComponent>,
                                CCL::ECS::Write<TrailComponent>>
{
public:
    TrailAnimationSyncSystem() : IfSystem("TrailAnimationSyncSystem") {}

    void Update(float dt) override;

private:
    /**
     * @brief 親をたどり、最初に見つかった Animator を持つ祖先を返す。
     * @param transform トレイルの Entity の Transform。自分自身は調べない。
     * @return 見つかった祖先。無ければ InvalidEntityID。
     * @note 親の循環に備えて 32 段で打ち切る。
     */
    CCL::ECS::EntityID FindAnimatorAncestor(const TransformComponent &transform) const;

    // 今の再生時刻が、再生中のシーケンスのどれか1つの Trail 区間に入っているか
    bool IsInsideTrailEvent(const AnimatorComponent &animator) const;
};
