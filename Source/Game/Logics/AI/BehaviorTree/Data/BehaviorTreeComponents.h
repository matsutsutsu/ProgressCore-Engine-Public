#pragma once
// ===========================================================================
// 概要: BT を持つ個体の実行状態（BehaviorTreeComponent）と判断材料（AIBlackboard）。
//
// 責務:
//   - 使う木のパスと、共有されている木への参照を持つ
//   - 判断材料（blackboard）を持つ
//   - ノードごとの残り時間と再開位置、抽選の乱数の状態を持つ
//   - デバッグ用の評価結果・強制実行・一時停止の状態を持つ
//
// 備考: 木の実体は BTAssetCache が全エンティティで共有する。個体が持つのは実行状態と判断材料だけ。
//       ここに置くのはどの敵でも意味を持つものだけ。特定の敵にしか無いものはその敵の
//       Component へ置く（第2形態のしきい値 → BossPhase、行動の要求 → BossCommand）。
// ===========================================================================
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "SimpleMath.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Engine/ECS/Common/CCL_Types.h"

// AI の判断材料（blackboard）。
// BossPerceptionSystem が毎フレーム書き、BT の条件・行動が読む（accumulatedDamage だけは A_ResetDamage も 0 に戻す）。
struct AIBlackboard
{
    CCL::ECS::EntityID           targetEntity = CCL::ECS::InvalidEntityID; // 狙っている相手。ID も持つのは、行動側が HP や状態も読めるようにするため
    DirectX::SimpleMath::Vector3 targetPosition;                           // 狙っている相手の足元（ワールド）
    float                        distanceToTarget = 0.0f;                  // 相手までの距離 [m]

    // 自分の正面から見た相手の角度（度）。正=右、負=左、±180=真後ろ。
    // 距離だけだと「正面8m」と「背後8m」が同じ値になり、回り込まれたことが分からない。
    float angleToTarget = 0.0f;

    // 相手が自分へ近づく速さ（m/s）。正=接近、負=後退。
    // 自分の移動は含めない。含めると、逃げる相手を追いかけている自分の速度が加わって
    // 相手が近づいてきていると誤判定する。知りたいのは相手の動きなので、相手の速度だけを使う。
    float targetClosingSpeed = 0.0f;

    // ステージの端（BattleContext の円）までの距離（m）。正=内側、負=外へはみ出している。
    // 中心からの距離ではなく端までの距離で持つのは、ステージの大きさを変えても
    // 「端から 8m 以内」という木の書き方がそのまま使えるようにするため。
    float distanceToArenaEdge = 9999.0f;

    // 自分から見た「ステージ中央の方向」と「相手の方向」のなす角（度）。0〜180。
    // 0 に近いほど相手が自分と中央の間に立っている（逃げ道を塞がれている）。
    float targetAngleFromCenter = 180.0f;

    float healthPercentage  = 100.0f; // 残り HP の割合（%、0〜100）
    float accumulatedDamage = 0.0f;   // 前回 A_ResetDamage してからの被弾量（ダメージの値そのもの）

    // accumulatedDamage を最大 HP で割ったもの（%）。しきい値は BT ノードの param（最大 HP に対する %）
    // で決める。ダメージ量のまま比べると、最大 HP を変えたときにしきい値も直す必要がある。
    float accumulatedDamageRatio = 0.0f;

    // 直近の被弾からの経過時間（秒）。被弾は今の行動を中断せず、次に何をするかの
    // 判断にだけ影響させる。残り時間ではなく経過時間なのは、待つ長さを木が決めるため。
    float timeSinceLastHit = 9999.0f;
};

// BT の実行状態。木そのもの（sharedAsset）は全個体で共有し、個体が持つのはどこまで進んだかだけ。
struct BehaviorTreeComponent
{
    // --- ツリーデータ ---

    // 今評価している木。BTAssetCache がパスごとに1つだけ持ち、全エンティティで共有する。
    // 書くのは BehaviorTreeSystem::SyncAssets だけ（他から書くと実行状態の作り直しが行われない）。
    std::shared_ptr<const BTAsset> sharedAsset;

    std::string assetPath;       // 使いたい木のパス。書き換えると次のフレームに SyncAssets が差し替える。空なら評価しない
    std::string loadedAssetPath; // 実際に読み込めた木のパス。表示用

    // --- 判断材料 ---

    AIBlackboard blackboard; // BossPerceptionSystem が書く判断材料

    // --- 実行状態（配列は木と同じ長さ。確保は BehaviorTreeSystem::SyncAssets）---

    std::vector<float> nodeTimers;       // ノードごとの残り時間 [秒]。-1 は未開始、0 は時間切れ
    std::vector<int>   runningNodes;     // 複合ノードの再開位置。次にどの子から再開するか。-1 は先頭から
    BTNodeID           runningNodeId = 0; // 今フレーム Running を返した最初のノード。0xFFFF は無し

    // WeightedSelector の抽選に使う乱数の状態。0 はまだ種を決めていない。
    //
    // 個体ごとに持つ主な理由は、評価が ForEachWithIDParallel で並列に走るため。
    // グローバルに1つ置くと、複数のワーカースレッドが同じ state を同時に読み書き
    // してデータ競合になる。共有される木（BTAsset）に残り時間を置けないのと
    // 同じ理由で、書き換わるものは個体が持つ。
    //
    // その結果、個体の乱数列はスレッドの割り当てに左右されない。
    // ただし dt が可変で、木の通る経路（抽選を引く回数）が毎回変わるため、
    // これだけで同じ試合を再現できるわけではない。値を見る・種を設定し直す手段も
    // 今は無いので、再現性が必要になったらまずそれを用意すること。
    uint32_t rngState = 0;

    // --- デバッグ（実行時のみ・非シリアライズ）---

    std::vector<BTDebugState> debugNodeStates;              // 今フレームの各ノードの評価結果。BTウィンドウの色分けに使う
    ActionID                  lastLogActionId          = 0xFFFF; // 最後に実行した行動。デバッガの見出しに出す
    int                       debugForcedNodeId        = -1;     // この枝だけを評価する（「今すぐ出す」）。-1 で通常どおりルートから
    int                       debugAppliedForcedNodeId = -1;     // 上を適用済みか。切り替わった瞬間に実行状態を捨てるのに使う
    bool                      debugPaused              = false;  // 木の評価を止める（強制実行中は無効）
};
