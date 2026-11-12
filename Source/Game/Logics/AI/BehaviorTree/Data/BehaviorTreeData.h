#pragma once
// ===========================================================================
// 概要: BT の木そのもの（BTAsset / BTNode）と、ノードの種類・評価結果の定義
//
// 備考: 子はポインタでなく配列の添字（BTNodeID）で持つ
//         - JSON にそのまま書ける。ポインタは保存できない
//         - 個体ごとの実行状態を、木と同じ長さの配列で持てる
//         - ノードの確保が1回にまとまる。ただし children は vector なので、ノードごとの確保は残る
// ===========================================================================
#include <cstdint>
#include <string>
#include <vector>

using BTNodeID = uint16_t; // ノードのインデックス（配列の添字）
using ActionID = uint16_t; // 行動・条件の種類を判別するID
using AssetID  = uint32_t; // BTAsset の種類を判別するID

// デバッグ用のノード状態（実行時の可視化専用）
enum class BTDebugState : uint8_t
{
    None = 0, // 今フレームは評価されていない
    Success,  // 成功を返した
    Failure,  // 失敗を返した
    Running   // 実行中を返した
};

/**
 * @brief 修飾の種類。Cooldown 以外は、まだアセットで使っていない
 *
 * 「子を開始させて手を離す」修飾（Async）は置かない。技は評価されている間だけ currentActionId を
 * 書き直す作りなので、手を離した次のフレームに要求が消え、体が技を出し切れないため
 * 同じ用途は BTNodeType::Parallel（主は評価され続ける）で書くこと
 */
enum class BTDecoratorType : uint8_t
{
    Inverter = 0, // 結果を反転 (Success <-> Failure)。Running は反転しない
    Cooldown = 1, // 子が成功してから param 秒が経つまで Failure
    Retry    = 2, // 失敗したら同じフレームで param 回まで試し直す。条件の子には効かない
    Repeat   = 3  // 子が成功するたびに数え、param 回に届くまで子をやり直す
};

/**
 * @brief ノードの種類。各種類の評価の詳細は BehaviorTreeSystem::EvaluateTreeRecursive の case に書いてある
 * @note アセットには文字列で保存する（BehaviorTreeLoader::StringToNodeType）ので、並びを変えても保存済みの木は壊れない
 */
enum class BTNodeType : uint8_t
{
    Selector,         // 順に評価し、最初に成功した子の結果を返す
    Sequence,         // 順に評価し、最初に失敗した子の結果を返す
    Condition,        // 条件を評価して成功/失敗を返す
    Action,           // 行動を実行して成功/失敗/実行中を返す
    Decorator,        // 子の結果を修飾する
    WeightedSelector, // 子の weight に比例して1つ引き、成功するまで引き直す
    Parallel,         // 0番目の子（主）の結果を返し、主が Running の間だけ残りの子も動かす
    UtilitySelector   // 今出せる子のうち、点数の高い上位3つから点数に比例して1つ引く
};

/**
 * @brief ノードの評価結果
 *
 * Running を最初に返すのは Action だけ（複合ノードは子の Running をそのまま返す）
 * Running を返したノードは、次のフレームも同じ所から評価される
 */
enum class BTNodeState : uint8_t
{
    Success, // 成功
    Failure, // 失敗
    Running  // 実行中。次のフレームも続ける
};

// WeightedSelector / UtilitySelector が抽選できる子の上限。候補バッファをスタックの固定長で取るため
// （評価はワーカースレッドで走るので、ヒープ確保を持ち込みたくない）
// 超えた木は読み込み時に警告する
inline constexpr int kMaxWeightedSelectorChildren = 16;

// 採点ルール1つが持てる状況による倍率の上限。共有の木にヒープを持ち込まないよう固定長で持つため
inline constexpr int kMaxScoreBonuses = 4;

// 状況による倍率1つ。条件（BT の条件ノードと同じもの）が通れば点数に mult を掛ける
struct BTScoreBonus
{
    ActionID conditionId = 0;    // 条件の ID（BT_CONDITION_LIST）
    float    param       = 0.0f; // 条件に渡す値（条件ノードの param と同じ意味）
    float    mult        = 1.0f; // 条件が通ったときに掛ける倍率
};

/**
 * @brief UtilitySelector の子の採点ルール。点数 = 優先度 × 距離の台形（0〜1）× 状況による倍率
 * @note ルールのない子は weight を点数にする。ルールは BTAsset::scoreRules に置き、ノードは添字だけを持つ
 */
struct BTScoreRule
{
    float base = 1.0f; // 優先度（ほかの技と比べた出やすさ）。満点の距離にいるときの点数

    // 距離の台形 [m]。bestMin〜bestMax で満点（×1）、範囲の外へ falloff かけて直線で 0 まで下がる
    // 既定値は「どの距離でも満点」
    float bestMin = 0.0f;    // 得意な距離の最小
    float bestMax = 9999.0f; // 得意な距離の最大
    float falloff = 0.0f;    // ぼかし幅。範囲の外へこれだけ離れると 0 点

    BTScoreBonus bonuses[kMaxScoreBonuses]; // 当てはまるものを全部掛ける
    uint8_t      bonusCount = 0;            // bonuses のうち使っている数

    // 選んでから次に候補へ戻るまで [秒]。0 はなし。数え始めは「選んだ瞬間」（技の最中も減る）
    // Cooldown の Decorator を挟まずに、技の判断の設定をこのルール1か所に集めるためのもの
    float cooldown = 0.0f;
};

/**
 * @brief 1つのノード定義
 * @note weight は param と別に、子が自分で持つ。param は技の長さなどで埋まっており、親に配列で持つと children と長さがずれうるため
 */
struct BTNode
{
    BTNodeType      type                = BTNodeType::Action;        // ノードの種類
    ActionID        actionOrConditionId = 0;                         // Action / Condition が指す行動・条件の ID
    BTDecoratorType decoratorType       = BTDecoratorType::Inverter; // 修飾の種類。Decorator のときだけ意味を持つ

    // 数値パラメータ。意味はノードの種類が決める
    //   Action    … その行動の引数（待機の秒数、近接の硬直など）
    //   Decorator … クールダウン秒数 / リトライ回数
    float param = 0.0f;

    float weight    = 1.0f;  // WeightedSelector / UtilitySelector の子としての選ばれやすさ。0 以下で候補外
    bool  interrupt = false; // 親が Selector 系のとき、ほかの枝が Running でも先頭の条件を見て割り込むか
    bool  abortSelf = false; // 親が Selector 系のとき、自分が Running の間も先頭の条件を見直し、崩れたら降りるか

    int16_t scoreRuleIndex = -1; // 採点ルール（BTAsset::scoreRules の添字）。-1 はなし

    std::vector<BTNodeID> children; // 子のノードID（実行順）
};

/**
 * @brief 全Entityで共有される木（アセット）。BTAssetCache が const で配る
 * @note 実行状態（残り時間・再開位置）を置いてはいけない。全個体が共有し、並列に評価するため。状態は BehaviorTreeComponent が持つ
 */
struct BTAsset
{
    AssetID                  id;         // アセットの ID（JSON の assetId）
    std::vector<BTNode>      nodes;      // 全ノード。nodes[0] が根
    std::vector<BTScoreRule> scoreRules; // 採点ルール。BTNode::scoreRuleIndex から引く

    // ノードの名前（エディタで付けたもの）。nodes と同じ長さで、名前のないノードは空
    // デバッグ表示だけが使う。BTNode に持たせないのは scoreRules と同じ理由（キャッシュライン）
    std::vector<std::string> nodeNames;
};

/**
 * @brief 枝の先頭に条件があるか（行動を実行せずに「今なら通るか」を調べられる形か）
 * @note 形の規則はここ1か所。読み込み時の検査と BehaviorTreeSystem::GuardsPass の両方が使うので、別に書かないこと
 */
inline bool HasReadableGuard(const BTAsset &asset, BTNodeID nodeId)
{
    if (nodeId >= asset.nodes.size()) return false;

    const BTNode &node = asset.nodes[nodeId];
    switch (node.type)
    {
    case BTNodeType::Condition: return true;

    // 修飾と手順は「先頭の子」へ降りる。Decorator(Cooldown) → Sequence[条件, …] という
    // よくある形が、そのまま2段降りて条件に行き着く
    case BTNodeType::Decorator:
    case BTNodeType::Sequence: return !node.children.empty() && HasReadableGuard(asset, node.children[0]);

    default: return false;
    }
}
