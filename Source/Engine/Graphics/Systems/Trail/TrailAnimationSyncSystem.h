#pragma once
// ===========================================================================
// 概要: トレイルの持ち主を探し、アニメの Trail 区間に合わせて emitting を切り替える System
//
// 責務:
//   - Animator を持つ祖先を探して ownerEntity に持つ。消えたら探し直す
//   - animControlled なら、今の再生時刻が Trail 区間に入っているかで emitting を決める
//
// 備考: L05_Collision で走る。AnimationSystem の後、
//       TrailRecordSystem と TrailSystem の前に走る
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Trail/TrailComponent.h"

class TrailAnimationSyncSystem : public CCL::ECS::IfSystem<
    TrailAnimationSyncSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<TrailComponent>>
{
public:
    TrailAnimationSyncSystem() : IfSystem("TrailAnimationSyncSystem") {}

    void Update(float dt) override;
};
