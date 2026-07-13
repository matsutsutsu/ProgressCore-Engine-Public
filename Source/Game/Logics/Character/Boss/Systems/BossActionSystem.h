/**
 * @file BossActionSystem.h
 * @brief AIの出力(BossCommandComponent)を読み取り、ボス本体の物理移動に変換する「筋肉」。
 *
 * ★最小構成版★
 *   Character/Boss/ に配置。AI（脳・BehaviorTreeSystem）とは明確に分離し、
 *   「AIの意図を物理に変換するだけ」の実行層としての責務に限定する。
 *
 * 対応するアクション（今回は最小構成）:
 *   currentActionId == A_Move … プレイヤーへ向かって移動
 *   それ以外（0）             … 待機。プレイヤーの方をゆっくり向く
 *
 * 移動パイプライン:
 *   BossCommand → BossActionSystem → CharacterMovementInputComponent
 *   → JoltCharacterUpdateSystem（CharacterVirtual）
 *
 * 実行順序: BehaviorTreeSystem（脳・L02_Update）より後、
 *           JoltCharacterUpdateSystem（L03_PrePhysics）より前 = L02_PostUpdate。
 */
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent
#include "Game/Logics/Character/Boss/Data/BossActionComponent.h"
#include "Engine/Timer/TimeState.h"

class BossActionSystem : public CCL::ECS::IfSystem<
    BossActionSystem,
    CCL::ECS::Write<CharacterMovementInputComponent>,
    CCL::ECS::Write<BossActionComponent>,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<BossCommandComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Read<TimeStateComponent>
>
{
public:
   BossActionSystem() : IfSystem("BossActionSystem") { hasGui = true; }
    ~BossActionSystem() override = default;
 
    void Update(float dt) override;
    void OnGui() override;

private:
    void ProcessIdle(BossActionComponent& bossDef, TransformComponent& trans,
                     CharacterMovementInputComponent& input,
                     const JoltCharacterHandleComponent& joltHandle,
                     const DirectX::SimpleMath::Vector3& playerPos, float dt);

    void ProcessMove(BossActionComponent& bossDef, TransformComponent& trans,
                     CharacterMovementInputComponent& input,
                     const JoltCharacterConfigComponent& config,
                     const JoltCharacterHandleComponent& joltHandle,
                     const DirectX::SimpleMath::Vector3& playerPos, float dt);
};
