// エディタが出力したJSONからランタイム用のBTAssetを構築する
#pragma once
#include "BehaviorTreeData.h"
#include <string>

class BehaviorTreeLoader
{
public:
    // JSONファイルからBTAssetを読み込む
    static bool LoadFromJson(const std::string &path, BTAsset &outAsset);

private:
    // 文字列をEnumのBTNodeTypeに変換する。
    static BTNodeType StringToNodeType(const std::string &typeStr);

    /// 木として辿れる形になっているかを確認する。通らなければ読み込みを中止する。
    /// @note ValidateNodes も EvaluateTreeRecursive も asset.nodes[...] を範囲チェック
    ///       せずに辿り、しかも再帰で降りる。壊れた木はログではなく異常終了で出るので、
    ///       ここだけは「警告して続行」ではなく「読まない」にする。
    static bool ValidateStructure(const BTAsset &asset, const std::string &path);

    /// 読み込めたが意図どおりには動かない形を報告する。
    /// @note 評価は並列で走るため実行時には気づけない。単一スレッドで、しかも
    ///       ファイル名を言えるここが唯一の検出場所。
    static void ValidateNodes(const BTAsset &asset, const std::string &path);

    /**
     * 1ノードぶんの行動/条件を決める。併記された保存キーが正で、数値IDは
     *        キーを引けなかったときの保険。
     * @param jNode  ノードのJSON（nlohmann::json だがヘッダに持ち込みたくないので void*）
     * @note  未知のキーは0（None）にせず、数値の方を採る。名前を打ち間違えただけで
     *        木が丸ごと無反応になるより、動いたまま警告が出る方が気づける。
     */
    static ActionID ResolveActionOrConditionId(const void *jNode, BTNodeType type,
                                               const std::string &path);
};
