#pragma once
// ===========================================================================
// 概要: BT エディタが書き出した JSON から、実行時に使う BTAsset を作るローダー。
//
// 責務:
//   - JSON のノードを読み、行動・条件を保存キーから ID に直す
//   - 範囲外の子・循環など、評価すると落ちる木を読み込まない
//   - 読めるが意図どおりに動かない形を警告する
// ===========================================================================
#include "BehaviorTreeData.h"
#include <string>

class BehaviorTreeLoader
{
public:
    // JSON ファイルから BTAsset を読み込む。失敗したら false
    static bool LoadFromJson(const std::string &path, BTAsset &outAsset);

private:
    // 文字列を BTNodeType に変換する。知らない文字列は Action として読む。
    static BTNodeType StringToNodeType(const std::string &typeStr);

    /**
     * @brief 木としてたどれる形になっているかを確認する。通らなければ読み込みを中止する。
     * @note ValidateNodes も EvaluateTreeRecursive も asset.nodes[...] を範囲チェック
     *       せずにたどり、しかも再帰で降りる。壊れた木はログではなく異常終了になるので、
     *       ここだけは「警告して続行」ではなく「読まない」にする。
     */
    static bool ValidateStructure(const BTAsset &asset, const std::string &path);

    /**
     * @brief 読み込めたが意図どおりには動かない形を報告する。
     * @note 評価は並列で走るので、実行中にはログを出せない。単一スレッドで、しかも
     *       ファイル名を出せるのはここだけ。
     */
    static void ValidateNodes(const BTAsset &asset, const std::string &path);

    /**
     * @brief 1ノード分の行動・条件の ID を決める。保存キーを優先し、数値 ID は
     *        キーから引けなかったときに使う。
     * @param jNode ノードの JSON（nlohmann::json だが、ヘッダに持ち込まないため void*）
     * @note 知らないキーは 0（None）にせず、数値の方を使う。名前を打ち間違えただけで
     *       木が丸ごと動かなくなるより、動いたまま警告が出る方が気づける。
     */
    static ActionID ResolveActionOrConditionId(const void *jNode, BTNodeType type,
                                               const std::string &path);
};
