/// @file  AIDebugSystem.h
/// @brief StateTree（司令官）が正しく動いているかを2秒ごとに診断するログシステム。
///        Drone Break 仕様に合わせて EnemyComponent 等の旧概念を排除。
///
/// 何を見るか:
///   STAGE-1 : StateTreeRegistry / BattleContext リソースの有無
///   STAGE-2 : StateTreeComponent を持つ Entity の handle 解決状態
///   STAGE-3 : どの戦術状態(Control/Pressure...)が選ばれているか
///   STAGE-4 : BehaviorTreeComponent の assetPath（BT切替が効いているか）
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeComponent.h"
#include "Engine/Timer/TimeState.h"

// StateTreeComponent と TimeStateComponent を持つ Entity を診断対象にする
class AIDebugSystem : public CCL::ECS::IfSystem<
                          AIDebugSystem,
                          CCL::ECS::Read<StateTreeComponent>,
                          CCL::ECS::Read<TimeStateComponent>>
{
  public:
    AIDebugSystem()
        : IfSystem("AIDebugSystem")
    {
    }
    void Update(float dt) override;

  private:
    float                  _timer    = 0.0f;
    static constexpr float kInterval = 2.0f; // 2秒ごとに出力
};