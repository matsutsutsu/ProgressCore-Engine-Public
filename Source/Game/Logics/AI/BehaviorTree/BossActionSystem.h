#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Animation/AnimParametersComponent.h"
#include "Game/Logic/AI/BehaviorTree/Data/BehaviorTreeComponents.h" 
#include "BossActionComponent.h"
#include "Game/Logic/Character/CharacterMovementInputComponent.h"
#include "Engine/GamePlay/Core/Time/TimeState.h"

#include <mutex> 
#include <vector> 

// ===================================================================================
// ファイル名: BossActionSystem.h
// 役割: AIの出力(BossCommand)を読み取り、ボスの物理移動とアニメーションに変換する実行部隊
// ===================================================================================

class BossActionSystem : public CCL::ECS::IfSystem<
    BossActionSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<AnimParametersComponent>,
    CCL::ECS::Write<BossActionComponent>,
    CCL::ECS::Write<CharacterMovementInputComponent>, 
    CCL::ECS::Write<BehaviorTreeComponent>,
    CCL::ECS::Read<BossCommandComponent>,
    CCL::ECS::Read<TimeState> 
>
{
public:
    BossActionSystem() : IfSystem("BossActionSystem") {}
    virtual ~BossActionSystem() override = default;

    virtual void Update(float dt) override;

private:
    // ========================================================================
     // [ コマンド受付 ]
     // ========================================================================
     /**
      * @brief AIからの新しい命令を受け取り、状態を更新する
      */
    void AcceptNewCommand(BossActionComponent& bossDef, const BossCommandComponent& cmd, TransformComponent& trans);

    // ========================================================================
    // [ アクション処理: 移動・基本系 ]
    // ========================================================================
    /**
     * @brief 待機中にプレイヤーを滑らかに注視する処理 (Idle)
     */
    void ProcessIdle(BossActionComponent& bossDef, TransformComponent& trans, const BossCommandComponent& cmd, float dt);

    /**
     * @brief プレイヤーへ向かって歩く処理 (Move)
     */
    void ProcessMove(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outCurrentSpeed);

    /**
     * @brief バックステップで距離を取る処理 (Evade)
     */
    void ProcessEvade(BossActionComponent& bossDef, float dt, float& outTargetSpeed);

    /**
     * @brief プレイヤーを睨みながら円周上を回り込む処理 (Circle Strafe)
     */
    void ProcessStrafe(BossActionComponent& bossDef, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outTargetSpeed);

    // ========================================================================
    // [ アクション処理: 攻撃・大技系 ]
    // ========================================================================
    /**
     * @brief 記憶した方向へ突進する処理 (Charge)
     */
    void ProcessCharge(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outTargetSpeed);

    /**
     * @brief プレイヤーの位置にジャンプして着地する処理 (Jump Attack)
     */
    void ProcessJumpAttack(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, float dt, float& outCurrentSpeed);

    // ========================================================================
    // [ アクション処理: 特殊・滞空・フェーズ移行系 ]
    // ========================================================================
    /**
     * @brief 原点の上空へジャンプし、ホバリング状態へ移行する (JumpToHover)
     */
    void ProcessJumpToHover(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, float dt, float& outTargetSpeed);

    /**
     * @brief ホバリング状態を解除し、プレイヤーの位置へ急降下着地する (HoverToLand)
     */
    void ProcessHoverToLand(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, const BossCommandComponent& cmd, float dt, float& outTargetSpeed);

    /**
     * @brief ステージ端から中央(0,0,0)へ強制的に仕切り直す大跳躍
     */
    void ProcessEscapeJump(BossActionComponent& bossDef, CharacterMovementInputComponent& moveInput, TransformComponent& trans, float dt, float& outTargetSpeed);

    /**
     * @brief 第2形態移行時の特殊演出処理 (PhaseTransition)
     */
    void ProcessPhaseTransition(CCL::ECS::EntityID bossId, BossActionComponent& bossDef,
        CharacterMovementInputComponent& moveInput, TransformComponent& trans,
        BehaviorTreeComponent& btComp, float dt, float& outTargetSpeed);

};