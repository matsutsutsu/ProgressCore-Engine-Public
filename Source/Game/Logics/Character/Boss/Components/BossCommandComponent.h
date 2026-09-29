#pragma once
// ===========================================================================
// 概要: ボスの BT が出した指示（本体の技・ドローンの陣形・攻撃信号）を持つ Component。
//
// 責務:
//   - ボス本体に出させたい技(currentActionId)とその全体時間を持つ
//   - ドローンに組ませたい陣形と、「今撃て」の信号を持つ
//
// 備考: BT のアクションが書き、BossActionSystem と DroneCommandSystem が読む。
//       BehaviorTreeComponent と分けているのは、同じ所に置くと敵を増やすたびに
//       汎用のデータへ敵ごとの出力フィールドが増えていくため。
// ===========================================================================
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h" // ActionID
#include "Game/Logics/AI/Drone/DroneComponent.h"               // DroneFormationType

struct BossCommandComponent
{
    // ドローンに組ませたい陣形。陣形指示のノード（DroneAction）が書く。
    DroneFormationType requestFormation = DroneFormationType::Hidden;

    // ボス本体に出させたい技。BossActionSystem が BossActionState へ変換する。0 は何もしない。
    ActionID currentActionId = 0;

    // currentActionId の技の全体時間（秒）。BT ノードの param（未設定なら技の既定値）。
    // 体の動きの長さを BT 側の数字に合わせたい技（滞空で頂点に留まる長さ）が読む。
    float currentActionDuration = 0.0f;

    // ドローンへの「今撃て」信号。DroneStrikeAction が立て、次の陣形指示が下ろす。
    bool droneAttackActive = false;

    // droneAttackActive の前フレームの値（DroneCommandSystem が書く）。立ち上がりを取り、
    // 個別に帰投する陣形の突撃を1回だけ始めるために使う
    // （無いと信号が立っている間ずっと全機が攻撃へ戻され、個別の帰投ができない）。
    bool prevDroneAttackActive = false;
};
