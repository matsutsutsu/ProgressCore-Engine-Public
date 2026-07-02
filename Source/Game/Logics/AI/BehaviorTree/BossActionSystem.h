#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" 
#include "BossActionComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Engine/Timer/TimeState.h"

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
    CCL::ECS::Read<TimeStateComponent> 
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

   

};