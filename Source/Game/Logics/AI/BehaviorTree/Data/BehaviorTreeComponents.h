// ECSエンティティが保持するAIの状態管理コンポーネント。
//
// 役割:
//   BTアセットのデータからその敵AIが「今どのノードを走っているか」
// 　の情報と「判断材料（Blackboard）」を持つ。
//
// なぜこの形か:
//   「思考の構造（アセット）」と「実行状態（コンポーネント）」を分けている。
//   木の実体は BTAssetCache が同じパスに1本だけ持ち、全Entityで共有する。
//   個体が抱えるのは実行状態だけなので、敵を増やしても木は増えない。
//
// 備考:
//   どの敵でも意味を持つものだけ。特定の敵にしか無い概念は、その敵のコンポーネントへ置く。
//     第2形態のBTパスとHP閾値 → BossPhaseComponent
//     ボスの行動要求・陣形要求 → BossCommandComponent
//   この線引きが崩れると、敵の種類を足すたびにこのファイルが太る。
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "SimpleMath.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Engine/ECS/Common/CCL_Types.h"


// AIの判断材料（ブラックボード）。
//
// 役割:
//   知覚システム（BossPerceptionSystem）が毎フレーム書き、BTの条件・行動が読む。
//   書き手はその1系統だけ。
//
// なぜ座標だけでなくターゲットIDを持つか:
//   以前は BattleContext.playerPos を全AIへ直接配っていて、「誰を狙うか」という概念が
//   コード上に存在しなかった（敵が1体しかいないので成立していた）。IDがあれば、
//   雑魚を群れで出したとき個体ごとに相手を変えられ、行動側が座標以外も引ける。
struct AIBlackboard {
    // --- ターゲット ---
    CCL::ECS::EntityID           targetEntity     = CCL::ECS::InvalidEntityID; // 狙っている相手
    DirectX::SimpleMath::Vector3 targetPosition;                               // その座標
    float                        distanceToTarget = 0.0f;                      // そこまでの距離

    // --- 自分の状態 ---
    float healthPercentage = 100.0f;    // 残りHPの割合（0〜100）

    // --- 被弾の蓄積（一定割合を受けたら距離を取る）---
    // 閾値そのものはここに持たない。BTノードの param（最大HPに対する％）で決める。
    //
    // なぜ絶対量ではなく割合を見るか:
    //   「20ダメージ受けたら退く」だと、ボスの最大HPを増やした瞬間に
    //   「HPを1%削っただけで退く」になる。強さの調整と回避の頻度が
    //   連動してしまい、片方を触るともう片方が壊れる。
    float accumulatedDamage      = 0.0f; // 前回リセットからの被弾量（実数値）
    float accumulatedDamageRatio = 0.0f; // 上を最大HPで割ったもの [%]。書き手は BossPerceptionSystem
};


// BehaviorTreeの実行状態を保持するコンポーネント
struct BehaviorTreeComponent {
    // --- ツリーデータ ---
    std::shared_ptr<const BTAsset> sharedAsset;  // 木の実体。BTAssetCache が全Entityで1本だけ持つ
    std::string assetPath;                       // 読み込むBTのパス。空なら評価しない
    std::string loadedAssetPath;                 // 実際に読み込めたパス。表示と差し替え判定に使う

    // --- 判断材料 ---
    AIBlackboard blackboard;

    // --- 実行状態（配列は木と同じ長さ。確保は BehaviorTreeSystem::SyncAssets）---
    std::vector<float> nodeTimers;               // ノードごとの残り時間。-1 は未開始
    std::vector<int>   runningNodes;             // 複合ノードのしおり。次にどの子から再開するか
    BTNodeID           runningNodeId = 0;        // 今フレーム Running を返した最初のノード

    // --- デバッグ（実行時のみ・非シリアライズ）---
    std::vector<BTDebugState> debugNodeStates;   // 今フレームの評価結果。BTウィンドウの色分けに使う
    ActionID lastLogActionId          = 0xFFFF;  // 最後に実行した行動。デバッガの見出しに出す
    int      debugForcedNodeId        = -1;      // この枝だけを評価する。-1 で通常どおりルートから
    int      debugAppliedForcedNodeId = -1;      // 上を適用済みか。切り替わった瞬間に実行状態を捨てる
    bool     debugPaused              = false;   // 木を凍らせる（上の説明を参照）
};
