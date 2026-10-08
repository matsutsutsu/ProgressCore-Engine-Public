#pragma once
// ===========================================================================
// 概要: ボス本体の動き(BossActionComponent)を、アニメーションのパラメータ(AnimParameters)へ書く System。
//
// 責務:
//   - Speed / IsMoving を書き、Idle と Move を切り替えさせる
//   - IsLeaping / LeapAttack を書き、跳躍の空中→着地を切り替えさせる
//   - DashPhase を書き、突進の段ごとにアニメを切り替えさせる
//   - BackStepPhase を書き、後方ステップの「下がる→下りる」を切り替えさせる
//
// 備考:
//   技の開始のトリガーは TriggeredMotionAction が引く。
//   Speed には Jolt の実速度ではなく BossActionSystem が決めた currentMoveSpeed を書く
//   （跳躍・突進中は 0 にして Idle/Move を動かさないため）。
//   実行ステージは L02_PreUpdate。AnimParameters を読む AnimControllerSystem が L02_Update なので、
//   同じステージに置くと登録順によっては1フレーム遅れる。
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
    BossAnimSyncSystem()
        : IfSystem("BossAnimSyncSystem")
    {
    }

    // CharacterAnimSyncSystem も同じ L02_PreUpdate でボスの Speed を（Jolt の実速度から）書く。
    // その後に走り、BossActionSystem が決めた速度で必ず上書きする。
    std::vector<const char *> GetRunAfter() const override { return {"CharacterAnimSyncSystem"}; }

    // 全ボスのアニメのパラメータを、今の動きに合わせて書き直す。
    void Update(float dt) override;
};
