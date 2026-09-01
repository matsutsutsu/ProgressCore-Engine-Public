// ボスAI（BT）の「出力」を受け取るコンポーネント。脳と筋肉の間の伝票。
//
// ここに置いてある理由:
//   以前は BehaviorTreeComponents.h（AI/BehaviorTree/Data）に同居していた。
//   BTエンジンは「ボスを知らない」と謳っている一方で、データ層がボスとドローンを
//   知っている状態になっていた（BehaviorTreeComponents.h が DroneComponent.h を
//   include していたのがその証拠）。敵の種類を増やすと、この1ファイルに
//   敵ごとの出力フィールドが積み上がる。
//   そこで「BTが読み書きする汎用データ」と「ボス固有の出力」を物理的に分けた。
//
// 書き手 / 読み手:
//   書き手 … ボス用の IBTAction（BTActions_Boss.h）、BossPhaseSystem、デバッグUI
//   読み手 … BossActionSystem（物理へ翻訳）、DroneCommandSystem（群れへ配布）
#pragma once
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h" // ActionID
#include "Game/Logics/AI/Drone/DroneComponent.h"               // DroneFormationType

// AIシステムからの「出力」を受け取るコンポーネント。
//        毎フレーム上書きされる使い捨てデータ（＝AIが今やってほしいこと）。
struct BossCommandComponent {
    DroneFormationType requestFormation = DroneFormationType::Hidden;

    // AIが現在「筋肉にやってほしい」と要求している ActionID。
    //        BossActionSystem がこれを BossActionState へ翻訳する。
    ActionID currentActionId = 0;

    // ドローン陣形攻撃の「今撃て」信号（実行時専用・非シリアライズ）。
    //        BTの DroneStrikeAction が true、DroneAction(陣形指示) が false にする。
    //        DroneCommandSystem が各ドローンの wantsAttack へ配布する。
    bool droneAttackActive = false;

    // 前フレームの droneAttackActive（実行時専用・非シリアライズ）。
    //        書き手は DroneCommandSystem のみ。信号の「立ち上がり」を検出して
    //        個別寿命の突撃（DroneAttackPhase::Charging）を1回だけ発火させるために使う。
    //        これが無いと、信号が立っている間ずっと全機が攻撃へ引き戻され、
    //        飛び切った機の個別帰投が成立しない。
    bool prevDroneAttackActive = false;
};
