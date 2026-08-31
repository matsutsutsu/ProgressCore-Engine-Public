/// @file  BossPerceptionSystem.h
/// @brief ボスの「状況認識」をBlackboardへ書き込む専用システム。
///
/// 役割分担（BehaviorTreeSystemを汎用のまま保つための分離）:
///   BossPerceptionSystem（このファイル） … ボス固有の「今の状況」を認識する
///     - HP割合を blackboard.healthPercentage に
///     - プレイヤーとの距離を blackboard.distanceToTarget に
///   BehaviorTreeSystem（汎用）           … blackboard を読んで判断するだけ
///
/// これにより BehaviorTreeSystem は HealthComponent や「プレイヤー検索」を
/// 一切知らないままでいられる（ドローン等、HPや対象の概念が違うAIにも使い回せる）。
///
/// 実行順序: L02_PreUpdate（AIの感知フェーズ）。
///   読み手はBTの条件・行動（L02_Update）だけなので、その手前であればよい。
///   BossPhaseSystem・DroneBrainSystem と同じ「L02_Update の入力を確定させる」役なので、
///   同じステージに揃えてある（以前は L01_Input にいたが、あそこは入力受付の段）。
///   前段の BattleContext は L01_WorldState で確定済み。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Time/TimeState.h"


class BossPerceptionSystem : public CCL::ECS::IfSystem<
    BossPerceptionSystem,
    CCL::ECS::Write<BehaviorTreeComponent>,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Read<HealthComponent>,
    CCL::ECS::Read<TimeStateComponent>
>
{
public:
    BossPerceptionSystem() : IfSystem("BossPerceptionSystem") {}
    void Update(float dt) override;
};