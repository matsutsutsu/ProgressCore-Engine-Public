#pragma once
// ===========================================================================
// 概要: トレイルの持ち主（Animator を持つ祖先）を見つけ、アニメーションの Trail 区間
//       イベントに合わせて TrailComponent の emitting を切り替える System（L05_Collision）
//
// 責務:
//   - すべてのトレイルについて、Animator を持つ祖先を探して ownerEntity に持つ
//   - 持ち主が消えたら ownerEntity を解き、次のフレームで探し直す
//   - animControlled なトレイルは、持ち主の今の再生時刻が Trail 区間に入っているかで emitting を決める
//
// 備考: 親をたどって最初に見つかった Animator の持ち主を owner にするので、インスペクタで指定しなくてよい
//       animControlled でないトレイルも owner を探す。TrailRecordSystem が一時トレイルの時計に使うため
//       AnimationSystem の後、TrailRecordSystem と TrailSystem の前に走る
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"

struct AnimatorComponent;

class TrailAnimationSyncSystem : public CCL::ECS::IfSystem<
    TrailAnimationSyncSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<TrailComponent>>
{
public:
    TrailAnimationSyncSystem() : IfSystem("TrailAnimationSyncSystem") {}

    void Update(float dt) override;

private:
    // 親をたどり、最初に見つかった Animator を持つ祖先を返す（自分は調べない）。なければ InvalidEntityID
    // 親の循環に備えて、32段で打ち切る
    CCL::ECS::EntityID FindAnimatorAncestor(const TransformComponent &transform) const;

    // 今の再生時刻が、再生中のシーケンスのどれか1つの Trail 区間に入っているか
    bool IsInsideTrailEvent(const AnimatorComponent &animator) const;
};
