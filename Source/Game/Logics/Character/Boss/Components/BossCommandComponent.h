#pragma once
// ===========================================================================
// 概要: ボスの BT が出した指示を持つ Component。
//       （本体の技・ドローンの陣形・攻撃信号）
//       BT のアクションが書き、
//       BossActionSystem と DroneCommandSystem が読む。
// ===========================================================================
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h" // ActionID
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"     // DroneFormationType

struct BossCommandComponent
{
    DroneFormationType requestFormation = DroneFormationType::None; // ドローンに組ませたい陣形

    // 本体に出させたい技。0 は何もしない。
    // BossPhaseSystem が毎フレーム 0 に戻す
    ActionID currentActionId       = 0;
    float    currentActionDuration = 0.0f; // 技の全体時間 [秒]

    bool droneAttackActive     = false; // ドローンへの「今撃て」信号
    bool prevDroneAttackActive = false; // 前フレームの信号（立ち上がり検出）
};
