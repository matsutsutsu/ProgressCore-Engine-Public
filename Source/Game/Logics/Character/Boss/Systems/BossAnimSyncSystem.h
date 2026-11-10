#pragma once
// ===========================================================================
// 概要: ボスの動き(BossActionComponent)を、アニメのパラメータへ書く System
//
// 責務:
//   - IsMoving で、待機と歩きを切り替えさせる
//   - SideTravelPhase / IsLeaping / IsLeapAttack / DashPhase / BackStepPhase で、技の段ごとにアニメを切り替えさせる
//   - FootworkDir で様子見に入る・出るを決めさせる
//   - MoveX / MoveY で、様子見と横移動の前後左右のクリップを混ぜさせる
//   - CanFlinch で、ひるみの冷却中はひるませない
//
// 備考: 技を始めるトリガーは BossActionSystem が引く
//       グラフは歩きを Speed でなく IsMoving で決める（実速度だと横移動や技でも Move に入る）
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

class BossAnimSyncSystem : public CCL::ECS::IfSystem<
    BossAnimSyncSystem,
    CCL::ECS::Read<BossActionComponent>,
    CCL::ECS::Write<AnimParametersComponent>>
{
public:
    BossAnimSyncSystem() : IfSystem("BossAnimSyncSystem") {}

    // 書く値は重ならないが、同じステージなので前後を決めておく
    std::vector<const char *> GetRunAfter() const override { return {"CharacterAnimSyncSystem"}; }

    void Update(float dt) override;
};
