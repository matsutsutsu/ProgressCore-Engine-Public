// ビヘイビアツリーの静的データ構造。
//
// 子をポインタではなく配列の添字(BTNodeID)で持つ。理由は3つ。
//   ・JSONにそのまま書ける。ポインタは保存できないので、ポインタ木にすると
//     保存と読み込みのたびに「ポインタ⇔添字」の変換が要る
//   ・ノードIDがそのまま添字になるので、個体ごとの実行状態を木と同じ長さの
//     配列（BehaviorTreeComponent の nodeTimers / runningNodes）で持てる
//   ・ノード本体の確保が「ノードの数だけ」から1回にまとまり、添字の近い
//     ノードが同じキャッシュラインに載る
//
// 誇張しないための但し書き。children は std::vector なので、子を辿るときの
// ヒープ参照はノードごとに1回残る（確保回数は N → 1+N であって 1 ではない）。
// また木の走査は深さ優先で添字が飛ぶため、連続アクセスを前提とする
// ハードウェアプリフェッチが効く形ではない。効いているのは空間的局所性の方。
#pragma once
#include <cstdint>
#include <vector>

using BTNodeID = uint16_t; // ノードのインデックス（配列の添字）
using ActionID = uint16_t; // 行動・条件の種類を判別するID
using AssetID  = uint32_t; // BTAsset の種類を判別するID

// デバッグ用のノード状態（実行時の可視化専用）。
enum class BTDebugState : uint8_t
{
    None = 0,
    Success,
    Failure,
    Running
};

enum class BTDecoratorType : uint8_t
{
    Inverter = 0, // 結果を反転 (Success <-> Failure)
    Cooldown = 1, // クールダウン (指定秒数経過するまでFailure)
    Retry    = 2, // 失敗しても指定回数リトライする
    Async    = 3  // 非同期実行（筋肉を待たずに即Successを返す）
};

enum class BTNodeType : uint8_t
{
    Selector,      // 順に評価し、最初に成功した子の結果を返す
    Sequence,      // 順に評価し、最初に失敗した子の結果を返す
    Condition,     // 条件を評価して成功/失敗を返す
    Action,        // 行動を実行して成功/失敗/実行中を返す
    Decorator,     // 子の結果を修飾する
    RandomSelector // ランダムな順で評価し、最初に成功した子の結果を返す
};

// ノードの評価結果。Running は Action だけが返しうる（次フレームも同じノードを評価する）。
enum class BTNodeState : uint8_t
{
    Success,
    Failure,
    Running
};

// RandomSelector が抽選できる子の上限。並べ替えバッファをスタックの固定長で取るため
// （評価はワーカースレッドで走る）。超えた木は読み込み時に警告する。
inline constexpr int kMaxRandomSelectorChildren = 16;

// 1つのノード定義。既定値は、読み込み側が代入を1つ忘れても不定値を読まないために書く。
struct BTNode
{
    BTNodeType type = BTNodeType::Action;

    ActionID actionOrConditionId = 0;

    // Decorator のときだけ意味を持つ修飾の種類。
    BTDecoratorType decoratorType = BTDecoratorType::Inverter;

    // 数値パラメータ。意味はノードの種類が決める。
    //   Action    … その行動の引数（待機の秒数、近接の硬直など）
    //   Decorator … クールダウン秒数 / リトライ回数
    float param = 0.0f;

    std::vector<BTNodeID> children;
};

// 全Entityで共有される不変データ（アセット）。
//
// 実行状態（残り時間・どの子まで進んだか）をここへ置いてはいけない。同じ木を使う
// 個体が1つの変数を奪い合い、片方が始めた待機がもう片方のぶんまで減る。評価は
// 並列で走るので、同じ float への同時書き込みにもなる。
// 状態は個体側（BehaviorTreeComponent）が、ノードIDをそのまま添字にした配列で持つ。
// BTAssetCache が const で配ることで、「書けない」ことを型で守っている。
struct BTAsset
{
    AssetID             id;
    std::vector<BTNode> nodes;
};
