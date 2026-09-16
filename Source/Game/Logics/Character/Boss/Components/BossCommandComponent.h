// ボスAI（BT）の出力を受け取る、脳と筋肉の間の伝票。毎フレーム上書きされる。
// BTのデータ層と分離してあるのは、同居させると敵を増やすたびに汎用の1ファイルへ
// 敵ごとの出力フィールドが積み上がるため。
#pragma once
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h" // ActionID
#include "Game/Logics/AI/Drone/DroneComponent.h"               // DroneFormationType

struct BossCommandComponent
{
    DroneFormationType requestFormation = DroneFormationType::Hidden;

    // 「筋肉にやってほしい」行動。BossActionSystem が BossActionState へ翻訳する。
    ActionID currentActionId = 0;

    // ドローンへの「今撃て」信号。DroneStrikeAction が立て、陣形指示が下ろす。
    bool droneAttackActive = false;

    // 上の前フレーム値。立ち上がりを取って個別寿命の突撃を1回だけ発火させる
    // （無いと信号が立っている間ずっと全機が攻撃へ引き戻され、個別帰投が成立しない）。
    bool prevDroneAttackActive = false;
};
