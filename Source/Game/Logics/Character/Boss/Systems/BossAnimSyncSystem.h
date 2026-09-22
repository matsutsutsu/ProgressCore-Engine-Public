// ボスのAI状態を移動アニメの掲示板(AnimParameters)へ翻訳する。技の一発トリガーは
// TriggeredMotionAction が引き、ここは Idle / Move の切替と、跳躍の空中→着地の切替を
// 書く唯一の書き手。
//
// 物理速度ではなく「AIが出したい速度」を載せるのは、ボスが Jolt Character を持たず
// CharacterAnimSyncSystem の対象外だから。物理由来のジッタが混ざらない利点もある。
//
// L02_PreUpdate。消費側の AnimControllerSystem が L02_Update なので、同ステージに
// 居ると登録順次第で1フレーム遅れになる。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

/// BossActionComponent の状態を、アニメグラフのパラメータ
/// （Speed / IsMoving / IsLeaping / LeapAttack）へ写す。
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

    /// 全ボスのアニメパラメータを、今の動きに合わせて書き直す。
    void Update(float dt) override;
};
