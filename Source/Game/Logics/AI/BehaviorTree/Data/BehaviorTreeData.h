// ビヘイビアツリー（BT）の静的データ構造の定義
//
// 役割:
//   AIの論理的な「思考構造」をメモリ上に定義する。
//
// なぜこの形か（DOD）:
//   伝統的なポインタベースの木構造（OOP）を完全に排除。
//   すべてのノードを固定長の構造体（BTNode）として定義し、配列（std::vector）に配置する。
//   これにより、ポインタジャンプ（Pointer Chasing）をなくし、L1/L2キャッシュへの
//   プリフェッチ効率を最大化している
#pragma once
#include <cstdint>
#include <vector>

// 厳格な型付け
// BTNodeID: ノードのインデックス（配列の添字）
// ActionID: 行動・条件の種類を判別するID
// AssetID: BTAssetの種類を判別するID
using BTNodeID = uint16_t;
using ActionID = uint16_t;
using AssetID = uint32_t;

// デバッグ用のノード状態（シリアライズ不要、実行時の可視化専用）
enum class BTDebugState : uint8_t {
    None = 0,
    Success,
    Failure,
    Running
};

// デコレーターの種類
enum class BTDecoratorType : uint8_t {
    Inverter = 0, // 結果を反転 (Success <-> Failure)
    Cooldown = 1, // クールダウン (指定秒数経過するまでFailure)
    Retry = 2,     // 失敗しても指定回数リトライする
    Async = 3     // 非同期実行（筋肉を待たずに即Successを返す）
};

// ノードの種類
// Selector: 子ノードを順番に評価し、最初に成功した子ノードの結果を返す
// Sequence: 子ノードを順番に評価し、最初に失敗した子ノードの結果を返す
// Condition: 条件を評価し、成功/失敗を返す
// Action: 行動を実行し、成功/失敗/実行中を返す
// Decorator: 子ノードの結果を修飾する（Inverter, Cooldown, Retry, Async）
// RandomSelector: 子ノードをランダムに評価し、最初に成功した子ノードの結果を返す
enum class BTNodeType : uint8_t {
    Selector,
    Sequence,
    Condition,
    Action,
    Decorator,
    RandomSelector 
};

// ノードの評価結果
// Success: 成功
// Failure: 失敗
// Running: 実行中（Actionノードのみが返す可能性がある状態。次フレームも同じノードを評価し続ける必要があることを示す）
enum class BTNodeState : uint8_t {
    Success,
    Failure,
    Running
};

// RandomSelector が抽選できる子の上限。
//
// 評価はワーカースレッドで走るので、抽選用の並べ替えバッファはスタックの固定長で取る
// （ヒープ確保を毎フレーム持ち込まないため）。上限を超えた木は読み込み時に警告する
// ＝ 実行中に黙って技が消えることはない（BehaviorTreeLoader が見張る）。
inline constexpr int kMaxRandomSelectorChildren = 16;

// 1つのノード定義
//
// @note 既定値を書いてあるのは、読み込み側が代入を1つ忘れても不定値を読まないため。
//       以前は全メンバが未初期化で、代入漏れが起きても静かに動いてしまう形だった。
struct BTNode {
    // ノードの種類（Selector, Sequence, Condition, Action）
	BTNodeType type = BTNodeType::Action;

    // Action/Conditionの種類を判別するID
    ActionID   actionOrConditionId = 0;

    // Decorator のときだけ意味を持つ修飾の種類
    BTDecoratorType decoratorType = BTDecoratorType::Inverter;

    // このノードの数値パラメータ。意味はノードの種類が決める。
    //        Action     … その行動の引数（待機の秒数、近接の硬直など）
    //        Decorator  … クールダウン秒数 / リトライ回数
    //
    // 1つに統合した理由:
    //   以前は「待機0.5秒」「待機1.0秒」…と、待ち時間の違いを12個の ActionID で
    //   表していた。引数をIDで表すとIDが際限なく増え、しかも中間の値を作れない。
    //   ノードが値を1つ持てば、待機は ActionID 1つで済む。
    float param = 0.0f;

    // 確実に子ノードのID（配列のインデックス）を保持するリストに変更
    std::vector<BTNodeID> children;
};

// 全Entityで共有される不変データ（アセット）
struct BTAsset {
    AssetID id;
    std::vector<BTNode> nodes;
};
