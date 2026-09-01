// ボスのAI状態（BossActionComponent）を移動アニメの掲示板(AnimParameters)へ翻訳する。
//
// 設計上の位置づけ（プレイヤーと対称）:
//   プレイヤー: PlayerAttackSystem(攻撃トリガー) + CharacterAnimSyncSystem(移動パラメータ)
//   ボス      : TriggeredMotionAction(技のトリガー) + BossAnimSyncSystem(移動パラメータ) ← これ
//
//   ・技(Slash / DashSlash / JumpAttack / BackStep)の一発トリガーは
//     TriggeredMotionAction が SetTrigger する（BTがその技を選んだ瞬間）。
//   ・Idle / Move の切替は、ここでボスの「意図(currentMoveSpeed/currentState)」を
//     Speed / IsMoving パラメータへ書き込み、AnimStateMachine が遷移する。
//
// なぜ物理速度でなく意図ベースか:
//   ボスは Jolt Character（JoltCharacterConfig）を持たないため、物理実速度から Speed を書く
//   汎用 CharacterAnimSyncSystem の対象外。つまりボスの Speed はここが唯一の書き手で、
//   「AIが出したい速度」を直接載せる。物理由来のジッタや押し出しが混ざらない。
//
// ステージ（掲示板の書き手は全員 PreUpdate に揃える）:
//   掲示板を消費する AnimControllerSystem は L02_Update=200。書き手が同じ L02_Update に
//   居ると登録順次第で「消費が先→1フレーム遅れ」になり得るため、Player 側の掲示板書き手
//   （CharacterAnimSync / PlayerAnimParamSync＝L02_PreUpdate）と同様に L02_PreUpdate へ置く。
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
