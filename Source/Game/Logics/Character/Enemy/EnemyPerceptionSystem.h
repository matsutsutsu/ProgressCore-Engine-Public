/// @file  EnemyPerceptionSystem.h
/// @brief 敵のプレイヤー知覚（距離・方向）を毎フレーム計算するセンサーシステム。
///
/// 実行順序（スケジューラが依存解決）:
///   このSystem (W PerceptionComponent)
///     → StateTreeSystem (R PerceptionComponent)
///       → EnemyMovementSystem (R MoveIntentComponent)
///
/// プレイヤー検索は Update 内で1回だけ行い、結果を全敵の Perception に配る。
/// （旧 EnemyTrackingSystem は敵ごとに同じ計算をしていたが、検索はフレーム1回で十分）
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "EnemyComponent.h"
#include "Game/Logics/Character/Enemy/PerceptionComponent.h"

// ※ namespace で囲まない（プロジェクト規約）
class EnemyPerceptionSystem : public CCL::ECS::IfSystem<
    EnemyPerceptionSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<EnemyComponent>,
    CCL::ECS::Write<PerceptionComponent>>
{
public:
    EnemyPerceptionSystem();
    void Update(float dt) override;
};