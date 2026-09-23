// AIの実行状態コンポーネント。「思考の構造（アセット）」と分けてあり、木の実体は
// BTAssetCache が全Entityで共有する。個体が抱えるのは実行状態と判断材料だけ。
//
// ここに置くのはどの敵でも意味を持つものだけ。特定の敵にしか無い概念はその敵の
// コンポーネントへ置く（第2形態の閾値→BossPhase、行動要求→BossCommand）。
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "SimpleMath.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"
#include "Engine/ECS/Common/CCL_Types.h"

/// AIの判断材料（ブラックボード）。
/// 知覚システムが毎フレーム書き、BTの条件・行動が読む。書き手はその1系統だけ。
struct AIBlackboard
{
    CCL::ECS::EntityID           targetEntity = CCL::ECS::InvalidEntityID; // 狙っている相手。IDも持つのは行動側がHPや状態も引けるように
    DirectX::SimpleMath::Vector3 targetPosition;                           // 狙っている相手の足元（ワールド）
    float                        distanceToTarget = 0.0f;                  // 相手までの距離 [m]

    /// 自分の正面から見た相手の角度 [度]。正=右、負=左、±180=真後ろ。
    /// 距離だけだと「正面8m」と「背後8m」が同じ値になり、回り込まれたことに気付けない。
    float angleToTarget = 0.0f;

    /// 相手が自分へ詰めてくる速さ [m/s]。正=接近、負=後退。
    /// 自分の移動は混ぜない。混ぜると、逃げる相手を追いかけている自分の速度が乗って
    /// 「相手が詰めてきている」と誤認する。読みたいのは相手の意思なので相手の速度だけ。
    float targetClosingSpeed = 0.0f;

    /// ステージの端（BattleContext の円）まであと何 m か。正=内側、負=外へはみ出している。
    /// 中心からの距離でなく端までの残りで持つのは、ステージの大きさを変えても
    /// 「端から 8m 以内」という木の書き方がそのまま通るようにするため。
    float distanceToArenaEdge = 9999.0f;

    /// 自分から見た「ステージ中央の方向」と「相手の方向」のなす角 [度]。0〜180。
    /// 0 に近いほど相手が自分と中央の間に立っている＝逃げ道を塞がれている。
    float targetAngleFromCenter = 180.0f;

    float healthPercentage  = 100.0f; // 残りHPの割合 [%]（0〜100）
    float accumulatedDamage = 0.0f;   // 前回 A_ResetDamage してからの被弾量（実数値）

    /// accumulatedDamage を最大HPで割ったもの [%]。閾値はBTノードの param（最大HPに対する％）
    /// で決める。絶対量で見ると、最大HPを増やした瞬間に「1%削っただけで退く」になる。
    float accumulatedDamageRatio = 0.0f;

    /// 直近の被弾からの経過 [秒]。被弾は「今の行動」を中断せず、「次に何をするか」の
    /// 判断にだけ影響させる。残り時間でなく経過時間なのは、待つ長さを木が決めるため。
    float timeSinceLastHit = 9999.0f;
};

/// ビヘイビアツリーの実行状態。木そのもの（sharedAsset）は全個体で共有し、
/// 個体が持つのは「どこまで進んだか」だけ。
struct BehaviorTreeComponent
{
    // --- ツリーデータ ---

    /// 今評価している木。BTAssetCache が全Entityで1本だけ持つ。
    /// 書き手は BehaviorTreeSystem::SyncAssets だけ（他から書くと実行状態のリセットがすり抜ける）。
    std::shared_ptr<const BTAsset> sharedAsset;

    std::string assetPath;       // 使いたい木のパス。書き換えると次のフレームに SyncAssets が差し替える。空なら評価しない
    std::string loadedAssetPath; // 実際に読み込めた木のパス。表示用

    // --- 判断材料 ---

    AIBlackboard blackboard; // 知覚システムが書く判断材料

    // --- 実行状態（配列は木と同じ長さ。確保は BehaviorTreeSystem::SyncAssets）---

    std::vector<float> nodeTimers;       // ノードごとの残り時間 [秒]。-1 は未開始、0 は時間切れ
    std::vector<int>   runningNodes;     // 複合ノードのしおり。次にどの子から再開するか。-1 は先頭から
    BTNodeID           runningNodeId = 0; // 今フレーム Running を返した最初のノード。0xFFFF は無し

    /// WeightedSelector の抽選に使う乱数の状態。0 は「まだ種を蒔いていない」。
    ///
    /// 個体ごとに持つ主な理由は、評価が ForEachWithIDParallel で並列に走るため。
    /// グローバルに1本置くと、複数のワーカースレッドが同じ state を同時に読み書き
    /// してデータ競合になる。共有される木（BTAsset）に残り時間を置けないのと
    /// まったく同じ理由で、書き換わるものは個体が持つ。
    ///
    /// 副産物として、その個体の乱数列はスレッドの割り当てに左右されなくなる。
    /// ただし dt が可変で、木の通る経路＝抽選を引く回数そのものが毎回変わるため、
    /// これだけで「同じ試合を再現できる」わけではない。値を見る・蒔き直す手段も
    /// 今は無いので、再現性を実際に使いたくなったらまずそれを用意すること。
    uint32_t rngState = 0;

    // --- デバッグ（実行時のみ・非シリアライズ）---

    std::vector<BTDebugState> debugNodeStates;              // 今フレームの各ノードの評価結果。BTウィンドウの色分けに使う
    ActionID                  lastLogActionId          = 0xFFFF; // 最後に実行した行動。デバッガの見出しに出す
    int                       debugForcedNodeId        = -1;     // この枝だけを評価する（「今すぐ出す」）。-1 で通常どおりルートから
    int                       debugAppliedForcedNodeId = -1;     // 上を適用済みか。切り替わった瞬間に実行状態を捨てるのに使う
    bool                      debugPaused              = false;  // 木を凍らせる（強制実行中は無効）
};
