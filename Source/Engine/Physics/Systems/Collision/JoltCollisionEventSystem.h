#pragma once
// ===========================================================================
// 概要: JoltContactListener に溜まった接触イベントを回収し、EventBus へ Publish する System。
//
// 責務:
//   - 物理更新（L04）の後、L05_Collision で接触イベントをまとめて取り出す
//   - 取り出したイベントを JoltCollisionEvent として EventBus へ流す
//
// 備考: 購読側（HitboxCollisionSystem / PlayerJustEvadeSystem）はハンドラ内でイベントを
//       自前の配列に積むだけにし、判定は自分の Update で行う。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

class JoltCollisionEventSystem
    : public CCL::ECS::IfSystem<JoltCollisionEventSystem>
{
public:
    JoltCollisionEventSystem() : IfSystem("JoltCollisionEventSystem") {}
    void Update(float dt) override;
};