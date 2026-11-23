#pragma once
// ===========================================================================
// 概要: BT を持つ個体の実行状態（BehaviorTreeComponent）と判断材料（AIBlackboard）
//
// 備考: 木の実体は BTAssetCache が全エンティティで共有し、個体は実行状態と判断材料だけを持つ
//       特定の敵にしかない値は、その敵の Component に置く（第2形態のしきい値は BossPhase など）
// ===========================================================================
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "SimpleMath.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Engine/ECS/Common/CCL_Types.h"

/**
 * @brief AI の判断材料（blackboard）
 *
 * BossPerceptionSystem が毎フレーム書き、BT の条件・行動が読む（accumulatedDamage だけは A_ResetDamage も 0 に戻す）
 * 値の決め方の理由は BossPerceptionSystem の各計算に書いてある
 */
struct AIBlackboard
{
    CCL::ECS::EntityID           targetEntity = CCL::ECS::InvalidEntityID; // 狙っている相手。行動側が HP や状態も読めるよう ID で持つ
    DirectX::SimpleMath::Vector3 targetPosition;                           // 狙っている相手の足元（ワールド）
    float                        distanceToTarget = 0.0f;                  // 相手までの距離（m）

    float angleToTarget         = 0.0f;    // 自分の正面から見た相手の角度（度）。正=右、負=左、±180=真後ろ
    float targetClosingSpeed    = 0.0f;    // 相手が自分へ近づく速さ（m/s）。正が接近、負が後退
    float targetLateralSpeed    = 0.0f;    // 相手が自分の周りを横へ回る速さ（m/s、0以上）。左右は問わない
    float targetSpeed           = 0.0f;    // 相手の水平の速さ（m/s）。止まりぎみかを見る
    float distanceToArenaEdge   = 9999.0f; // ステージの端までの距離（m）。正=内側、負=外へはみ出している
    float targetAngleFromCenter = 180.0f;  // 中央の方向と相手の方向のなす角（度）。0 に近いほど逃げ道を塞がれている
    float spaceOnLeft           = 9999.0f; // 相手を向いて真左へ、ステージの端まで進める距離（m）
    float spaceOnRight          = 9999.0f; // 相手を向いて真右へ、ステージの端まで進める距離（m）

    float healthPercentage       = 100.0f;  // 残り HP の割合（%、0〜100）
    float accumulatedDamage      = 0.0f;    // 前回 A_ResetDamage してからの被弾量（ダメージの値そのもの）
    float accumulatedDamageRatio = 0.0f;    // accumulatedDamage を最大 HP で割ったもの（%）
    float timeSinceLastHit       = 9999.0f; // 直近の被弾からの経過時間（秒）
    float battleTime             = 0.0f;    // 戦いが動いていた時間の合計（秒）。ムービー中・決着後は進まない
};

// UtilitySelector の子が候補に入ったか。入らなかったならその理由（デバッグ表示用）
enum class BTCandidateState : uint8_t
{
    NotScored,  // まだ採点していない
    Candidate,  // 候補に入った
    OutOfRange, // 距離の外（距離の倍率が 0）
    ZeroScore,  // 点数が 0（優先度か状況による倍率が 0）
    Cooldown,   // クールダウン中
    Condition,  // 先頭の条件が通らない
};

// 候補に入らなかった理由の表示名
inline const char *ToString(BTCandidateState state)
{
    switch (state)
    {
    case BTCandidateState::NotScored: return "未採点";
    case BTCandidateState::Candidate: return "候補";
    case BTCandidateState::OutOfRange: return "距離の外";
    case BTCandidateState::ZeroScore: return "点数が 0";
    case BTCandidateState::Cooldown: return "クールダウン中";
    case BTCandidateState::Condition: return "条件が通らない";
    }
    return "?";
}

// UtilitySelector が最後に技を選んだときの、子1つ分の点数（デバッグ表示用）
struct BTScoreDebug
{
    float            total        = -1.0f; // 点数。候補外の子も計算した値を入れる。-1 はまだ採点していない
    float            distance     = -1.0f; // 距離の倍率（0〜1）。-1 は「距離で点数を変える」を使っていない
    float            bonus        = 1.0f;  // 状況による倍率（当てはまったものの積）。1 は当てはまりなし
    float            repeat       = 1.0f;  // 繰り返しの減点の倍率（0.3〜1）。1 は減点なし
    float            chance       = 0.0f;  // 最初の抽選で選ばれる確率（0〜1）。上位3つの外は 0
    float            cooldownLeft = 0.0f;  // クールダウンの残り [秒]。技選択の記録に写すときだけ入れる
    BTCandidateState state        = BTCandidateState::NotScored; // 候補に入ったか・入らなかった理由
    bool             picked       = false; // 実際に選ばれた子か
    bool             interrupted  = false; // 割り込みで選ばれた子か（抽選していない）
};

// 技選択の記録1件に残す子の数。最初の抽選の上位3つ（kUtilityTopCount）＋繰り上がりで選ばれた子
// 候補が3つに足りないときは、空いた枠に候補外の子を入れる（その場合は繰り上がりが起きないので4つで足りる）
inline constexpr int kPickRecordRows = 4;

// 技選択の記録を何件まで残すか。いっぱいになったら古いものから上書きする
inline constexpr int kPickLogSize = 8;

// UtilitySelector が技を選んだときの記録（デバッグ表示用）
// debugScores は次に選び直すと消えるので、選んだ瞬間の内訳をここへ写して残す
struct BTPickRecord
{
    BTNodeID     selectorId = 0xFFFF;          // 選んだ UtilitySelector
    BTNodeID     pickedId   = 0xFFFF;          // 選ばれた子
    float        time       = 0.0f;            // 選んだ時刻（BehaviorTreeComponent::debugClock の値）[秒]
    float        distance   = 0.0f;            // 選んだときの相手までの距離 [m]
    int          rowCount   = 0;               // rowIds・rows のうち使っている数
    BTNodeID     rowIds[kPickRecordRows] = {}; // 子のノードID（点数の高い順）
    BTScoreDebug rows[kPickRecordRows];        // 子の点数の内訳
};

// BT の実行状態。木そのもの（sharedAsset）は全個体で共有し、個体が持つのはどこまで進んだかだけ
struct BehaviorTreeComponent
{
    // --- ツリーデータ ---

    // 今評価している木（BTAssetCache と共有）。書くのは BehaviorTreeSystem::SyncAssets だけ
    // 他から書くと実行状態の配列が作り直されず、木と長さが合わなくなる
    std::shared_ptr<const BTAsset> sharedAsset;

    std::string assetPath;       // 使う木のパス。書き換えると次のフレームに差し替わる。空なら評価しない
    std::string loadedAssetPath; // 実際に読み込めた木のパス。表示用

    // --- 判断材料 ---

    AIBlackboard blackboard; // BossPerceptionSystem が書く判断材料

    // --- 実行状態（配列は木と同じ長さ。確保は BehaviorTreeSystem::SyncAssets）---

    std::vector<float> nodeTimers;       // ノードごとの残り時間 [秒]。-1 は未開始、0 は時間切れ
    std::vector<int>   runningNodes;     // 複合ノードの再開位置。次にどの子から再開するか。-1 は先頭から
    std::vector<float> repeatTimers;     // UtilitySelector の子を最後に選んでからの減点の残り（秒）
    std::vector<float> cooldownTimers;   // UtilitySelector の子のクールダウンの残り（秒）。0 で候補に戻る
    BTNodeID           runningNodeId = 0; // 今フレーム Running を返した最初のノード。0xFFFF はなし
    uint32_t           rngState      = 0; // 抽選の乱数の状態。0 はまだ種を決めていない

    // --- デバッグ（実行時のみ・非シリアライズ）---

    std::vector<BTDebugState> debugNodeStates;              // 今フレームの各ノードの評価結果。BTウィンドウの色分けに使う
    std::vector<BTScoreDebug> debugScores;                  // UtilitySelector の子の点数。次に選び直すまで残る
    BTPickRecord              debugPickLog[kPickLogSize];   // 直近の技選択の記録。古いものから上書きする
    int                       debugPickCount           = 0;      // これまでに記録した数。最新は debugPickLog[(debugPickCount - 1) % kPickLogSize]
    float                     debugClock               = 0.0f;   // 木を評価した時間の合計（秒）。一時停止中は進まない
    ActionID                  lastLogActionId          = 0xFFFF; // 最後に実行した行動。デバッガの見出しに出す
    int                       debugForcedNodeId        = -1;     // この枝だけを評価する（「今すぐ出す」）。-1 で通常どおりルートから
    int                       debugAppliedForcedNodeId = -1;     // 上を適用済みか。切り替わった瞬間に実行状態を捨てるのに使う
    bool                      debugPaused              = false;  // 木の評価を止める（強制実行中は無効）
};
