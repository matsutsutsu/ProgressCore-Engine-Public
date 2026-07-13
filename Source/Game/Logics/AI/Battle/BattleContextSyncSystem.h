/// @file  BattleContextSyncSystem.h
/// @brief BattleContext（世界の事実）にプレイヤー・ボスの居場所を毎フレーム反映する。
///
/// 背景:
///   BattleContext.playerEntity/playerPos は誰かが書き込まないと
///   InvalidEntityID/ゼロ座標のままで、HasPlayer() が常に false になり、
///   これをガードにしている全システム（BossPerception, BossAction,
///   DroneFormation 等）が動かなくなる。
///   このシステムが「唯一の書き込み元」として毎フレーム更新する。
///
/// 設計方針:
///   1対1なので探索コストは軽微。View を2回（Player, Boss）回すだけ。
///   他のあらゆるAI/移動系システムより前に実行する必要があるため L01_Input。
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

/// @brief PlayerComponent をフィルタに使うが、実際の更新処理は Update 内で
///        World::View を直接使い、プレイヤー・ボスの両方を探して反映する。
class BattleContextSyncSystem : public CCL::ECS::IfSystem<
                                    BattleContextSyncSystem,
                                    CCL::ECS::Read<PlayerComponent>,
                                    CCL::ECS::Read<TransformComponent>>
{
  public:
    BattleContextSyncSystem()
        : IfSystem("BattleContextSyncSystem")
    {
    }
    void Update(float dt) override;
};