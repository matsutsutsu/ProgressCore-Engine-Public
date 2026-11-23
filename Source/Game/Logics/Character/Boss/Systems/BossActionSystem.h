#pragma once
// ===========================================================================
// 概要: BT が出した技（BossCommandComponent）を、ボス本体の動き（速度・向き）に変える System
//
// 責務:
//   - 技に入った瞬間に、進む向きを決めてアニメの開始のトリガーを引く
//   - 体の状態ごとに、技のクラス（Actions/）へ1フレーム分の処理を振り分ける
//   - 演出中・決着後・ひるみの間は技を始めずに待機する
//
// 備考: 流れは BossCommand → BossActionSystem → CharacterMovementComponent → JoltCharacterUpdateSystem
//       BT は技を出して actionFinished を待つだけで、技の長さは BossActionComponent の段ごとの時間が決める
//       currentActionId は BossPhaseSystem が毎フレーム空にするので、BT が止まれば待機になる
// ===========================================================================
#include <vector>

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/Character/Boss/Components/BossActionComponent.h"
#include "Game/Logics/Character/Boss/Systems/Actions/BossActionCommon.h"
#include "Game/Logics/Character/Boss/Systems/Actions/BossMoveAction.h"
#include "Game/Logics/Character/Boss/Systems/Actions/BossBackStepAction.h"
#include "Game/Logics/Character/Boss/Systems/Actions/BossRepositionAction.h"
#include "Game/Logics/Character/Boss/Systems/Actions/BossFootworkAction.h"
#include "Game/Logics/Character/Boss/Systems/Actions/BossLeapAction.h"
#include "Game/Logics/Character/Boss/Systems/Actions/BossDashAction.h"
#include "Engine/Time/Components/TimeState.h"

struct AnimStateMachineComponent; // ひるみ中かを読む

class BossActionSystem : public CCL::ECS::IfSystem<
    BossActionSystem,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Write<BossActionComponent>,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<BossCommandComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
    using Base = CCL::ECS::IfSystem<
        BossActionSystem,
        CCL::ECS::Write<CharacterMovementComponent>,
        CCL::ECS::Write<BossActionComponent>,
        CCL::ECS::Write<TransformComponent>,
        CCL::ECS::Read<BossCommandComponent>,
        CCL::ECS::Read<JoltCharacterConfigComponent>,
        CCL::ECS::Read<JoltCharacterHandleComponent>,
        CCL::ECS::Read<TimeStateComponent>>;

public:
    BossActionSystem() : IfSystem("BossActionSystem") {}
    ~BossActionSystem() override = default;

    void Update(float dt) override;
    void OnGui() override;

    std::vector<CCL::ECS::TypeID> GetReadTypes() const override;
    std::vector<CCL::ECS::TypeID> GetWriteTypes() const override;

private:
    // BT の技を変換した結果
    struct TranslatedAction
    {
        BossActionState    state   = BossActionState::None; // 体の動き
        const BossLeapDef *leapDef = nullptr;               // 跳び方（跳躍の技だけ。bossAction の中を指す）
    };

    // BT が出した技(ActionID) を、体の動き(BossActionState) と跳び方へ変換する。技を増やすときはここに1行足す
    // 知らない技は None
    TranslatedAction TranslateAction(ActionID actionId, const BossActionComponent &bossAction) const;

    // ひるみのアニメ（Hit_Reaction）を再生しているかを isFlinching に書き、明けてからの冷却を進める
    // ひるんでいる間、Update は BT の要求を読まずに待機にする
    void UpdateFlinch(BossActionComponent &bossAction, const AnimStateMachineComponent *animStateMachine, float dt) const;

    // 技に入った瞬間に1回だけ呼ぶ。前の技の移動を取り消し、技ごとの Enter を呼ぶ
    // leapDef は跳躍の技の跳び方、actionParam は技のノードの param（間合いを取るの距離）
    void OnEnterState(BossActionState nextState, const BossLeapDef *leapDef, float actionParam, BossActionComponent &bossAction,
                      const BossActionFrame &frame);

    // --- 技（体の状態ごとに1つ）---

    BossMoveAction       m_move;       // 待機・移動
    BossBackStepAction   m_backStep;   // 後方ステップ
    BossRepositionAction m_reposition; // 間合いを取る
    BossFootworkAction   m_footwork;   // 様子見
    BossLeapAction       m_leap;       // 跳躍（中央へ逃げる・跳躍攻撃・滞空）
    BossDashAction       m_dash;       // 突進攻撃
};
