#pragma once
// ===========================================================================
// 概要: アニメーションの Trail 区間イベントに合わせて、TrailComponent の emitting を
//       切り替える System（L05_Collision）。
//
// 責務:
//   - animControlled なトレイルについて、Animator を持つ祖先を探して ownerEntity に持つ
//   - 祖先の今の再生時刻が Trail 区間に入っているかで emitting を決める
//
// 備考: 走査するのは Animator 側ではなくトレイル側（CombatAnimationSyncSystem と同じ形）。
//       書き込むのは自分の TrailComponent だけで、祖先の Animator は GetComponent で読むだけ。
//       武器は BoneAttachment などでキャラの子孫に付いているので、親をたどって最初に
//       見つかった Animator の持ち主を owner にする（インスペクタで owner を指定しなくてよい）。
//       AnimationSystem（L02_PostUpdate）が再生時刻を進めた後、TrailRecordSystem（L06）と
//       描画側の TrailSystem（R08）が読む前に走るので、同じフレームで揃う。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"

class TrailAnimationSyncSystem
    : public CCL::ECS::IfSystem<TrailAnimationSyncSystem,
                                CCL::ECS::Read<TransformComponent>,
                                CCL::ECS::Write<TrailComponent>>
{
public:
    TrailAnimationSyncSystem() : IfSystem("TrailAnimationSyncSystem") {}

    void Update(float dt) override;
};
