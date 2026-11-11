#pragma once
// ===========================================================================
// 概要: 無敵時間を減らし、HP が 0 以下になったエンティティに DeadTag を付ける System
//
// 責務:
//   - invincibilityTimer をエンティティごとの時間（localDt）で減らす
//   - HP が 0 以下なら 0 にそろえ、DeadTag を付ける
//   - OnGui で、HealthComponent を持つ全エンティティの HP と無敵の状態を表にする
//
// 備考: 戦闘の流れでは DamageResolutionSystem（L06）の後に走る
//       誰がダメージを与えたかは見ず、HP だけで判定するので、ダメージの原因が増えても変えずに済む
//       localDt で減らすので、ヒットストップで止まっている間は無敵時間も減らない
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Engine/Time/Components/TimeState.h"

class HealthSystem : public CCL::ECS::IfSystem<
    HealthSystem,
    CCL::ECS::Write<HealthComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    HealthSystem() : IfSystem("HealthSystem") {}

    void Update(float dt) override;

    // HealthComponent を持つ全エンティティの HP と無敵の状態を表にする
    void OnGui() override;
};
