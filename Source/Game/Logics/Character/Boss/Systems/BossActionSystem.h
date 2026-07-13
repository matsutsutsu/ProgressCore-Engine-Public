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
 * 実行順序: BehaviorTreeSystem（脳・L02_Update）より後、
 *           物理同期（Push）より前 = L03_PrePhysics。
 */
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent
#include "Game/Logics/Character/Boss/Data/BossActionComponent.h"
#include "Engine/Timer/TimeState.h"

class BossActionSystem : public CCL::ECS::IfSystem<
    BossActionSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<BossActionComponent>,
    CCL::ECS::Read<BossCommandComponent>,
    CCL::ECS::Read<TimeStateComponent>
>
{
public:
   BossActionSystem() : IfSystem("BossActionSystem") { hasGui = true; }
    ~BossActionSystem() override = default;
 
    void Update(float dt) override;
    void OnGui() override;

private:
    /**
     * @brief 待機中にプレイヤーを滑らかに注視する処理 (Idle)
     */
    void ProcessIdle(BossActionComponent& bossDef, TransformComponent& trans,
                     const DirectX::SimpleMath::Vector3& playerPos, float dt);

    /**
     * @brief プレイヤーへ向かって歩く処理 (Move)
     */
    void ProcessMove(BossActionComponent& bossDef, TransformComponent& trans,
                     const DirectX::SimpleMath::Vector3& playerPos, float dt);
};