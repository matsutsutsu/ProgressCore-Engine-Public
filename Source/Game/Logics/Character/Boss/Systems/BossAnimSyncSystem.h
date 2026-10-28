#pragma once
// ===========================================================================
// 概要: ボス本体の動き(BossActionComponent)を、
//       アニメーションのパラメータ(AnimParameters)へ書く System。
//
// 責務:
//   - IsMoving を書き、Idle と Move を切り替えさせる
//   - StrafeDir を書き、横歩きの左右を切り替えさせる
//   - IsLeaping / LeapAttack を書き、跳躍の空中→着地を切り替えさせる
//   - DashPhase を書き、突進の段ごとにアニメを切り替えさせる
//   - BackStepPhase を書き、後方ステップの「下がる→下りる」を切り替えさせる
//
// 備考:
//   技の開始のトリガーは BossActionSystem が、技に入った瞬間に引く
//   （ここでは入った瞬間が分からない）。
//
//   Speed（Jolt の実速度）は CharacterAnimSyncSystem が書く。
//   グラフは歩くかどうかを Speed ではなく IsMoving で決める
//   （実速度だと、横歩きや技で動いている間も Move に入ってしまう）。
//
//   実行ステージは L02_PreUpdate。
//   AnimParameters を読む AnimControllerSystem が L02_Update なので、
//   同じステージに置くと、登録順によっては1フレーム遅れる。
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

    // CharacterAnimSyncSystem も同じ L02_PreUpdate で AnimParameters を書く。
    // 書く値は重ならないが、前後を決めておく（順序が不定という警告を出さない）
    std::vector<const char *> GetRunAfter() const override { return {"CharacterAnimSyncSystem"}; }

    // 全ボスのアニメのパラメータを、今の動きに合わせて書き直す。
    void Update(float dt) override;
};
