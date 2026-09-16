#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h" // 除外フィルタ用

// ===================================================================================
// ファイル名: JoltUpdateSystem.h
// 役割:
// 物理シミュレーションの進行(Step)と、結果の受信(Pull)を担う中核システム群。
//
// 【アーキテクチャ仕様】
// 1. JoltStepSystem
//    - 実行フェーズ: Physics
//    - JoltPhysicsManager::Step() を呼び、全CPUコアで並列に衝突・落下計算を実行する。
// 2. JoltPullSystem
//    - 実行フェーズ: Collision (または PostPhysics)
//    - 計算が終わったJolt剛体の最新座標を読み取り、ECSの Transform に書き戻す。
//    - CharacterVirtual を持つEntityは JoltCharacterUpdateSystem が同期済みのため、
//      SetExclude で Chunk 単位から丸ごと除外する（ループ内の毎エンティティ判定を撤廃）。
// ===================================================================================

// 1. 物理の時間を進めるシステム
class JoltStepSystem : public CCL::ECS::IfSystem<JoltStepSystem>
{
public:
    JoltStepSystem() : IfSystem("JoltStepSystem") {}
    void Update(float dt) override;
};

// 2. 計算結果をECSのTransformに同期するシステム
class JoltPullSystem : public CCL::ECS::IfSystem<JoltPullSystem,
                                                 CCL::ECS::Write<TransformComponent>,
                                                 CCL::ECS::Read<JoltHandleComponent>>
{
public:
    JoltPullSystem() : IfSystem("JoltPullSystem")
    {
        // キャラクターは CharacterUpdateSystem が既に正しい位置に同期している。
        // ここで Pull すると競合してカクつくため、Chunk ごと対象から外す。
        SetExclude<JoltCharacterHandleComponent>();
    }
    void Update(float dt) override;
};