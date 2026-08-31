/**
 * ボスのAI状態（BossActionComponent）を移動アニメの掲示板(AnimParameters)へ翻訳する。
 *
 * 設計上の位置づけ（プレイヤーと対称）:
 *   プレイヤー: PlayerAttackSystem(攻撃トリガー) + CharacterAnimSyncSystem(移動パラメータ)
 *   ボス      : TriggeredMotionAction(技のトリガー) + BossAnimSyncSystem(移動パラメータ) ← これ
 *
 *   ・技(Slash / DashSlash / JumpAttack / BackStep)の一発トリガーは
 *     TriggeredMotionAction が SetTrigger する（BTがその技を選んだ瞬間）。
 *   ・Idle / Move の切替は、ここでボスの「意図(currentMoveSpeed/currentState)」を
 *     Speed / IsMoving パラメータへ書き込み、AnimStateMachine が遷移する。
 *
 * なぜ物理速度でなく意図ベースか:
 *   汎用 CharacterAnimSyncSystem は Jolt の実速度から Speed を書くが（ボスにも適用される）、
 *   本システムは L02_Update でその後に走り、ボスに限り「AIが出したい速度」で上書きする。
 *   これにより物理のジッタや押し出しでアニメがちらつかず、ボスの意図どおりに動く。
 */
#pragma once
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
    void Update(float dt) override;
};
