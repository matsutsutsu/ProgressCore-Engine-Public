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
    ActionID currentActionId    = 0;
    float    currentActionParam = 0.0f; // 技のノードの param（横移動の距離など）

    // 技の依頼番号。BT が技を出し始めるたびに1増やす（0 に戻さない）
    // 同じ技を続けて出しても、本体は新しい技として始め直す
    uint32_t actionSerial = 0;

    bool droneAttackActive     = false; // ドローンへの「今撃て」信号
    bool prevDroneAttackActive = false; // 前フレームの信号（立ち上がり検出）
};
