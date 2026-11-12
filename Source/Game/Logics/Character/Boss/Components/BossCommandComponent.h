#pragma once
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"

// ボスの BT が出した指示（本体の技・ドローンの陣形・攻撃信号）
// BT のアクションが書き、BossActionSystem と DroneCommandSystem が読む
struct BossCommandComponent
{
    DroneFormationType requestFormation = DroneFormationType::None; // ドローンに組ませたい陣形

    // 本体に出させたい技。0 は何もしない
    // BossPhaseSystem が毎フレーム 0 に戻す
    ActionID currentActionId    = 0;
    float    currentActionParam = 0.0f; // 技のノードの param（横移動の距離など）

    // 技の依頼番号。BT が技を出し始めるたびに1増やす（0 に戻さない）
    // 同じ技を続けて出しても、本体は新しい技として始め直す
    uint32_t actionSerial = 0;

    bool droneAttackActive     = false; // ドローンへの「今撃て」信号
    bool prevDroneAttackActive = false; // 前フレームの信号（立ち上がり検出）

    // --- 怒り ---

    // 読む側は raging だけを見る。書くのは怒りの行動（A_BossRage）が rageRequested へ
    // BossPhaseSystem がフレームの最初に rageRequested を raging へ写し、false に戻す
    bool rageRequested = false; // このフレームに怒りの行動が評価されたか
    bool raging        = false; // 怒り中か（前のフレームに怒りの行動が評価された）
};
