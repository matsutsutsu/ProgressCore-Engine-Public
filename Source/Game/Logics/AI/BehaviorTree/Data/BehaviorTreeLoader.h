#pragma once
// ===========================================================================
// 概要: BT エディタが書き出した JSON から、実行時に使う BTAsset を作るローダー
//
// 責務:
//   - JSON のノードを読み、行動・条件を保存キーから ID に直す
//   - UtilitySelector の子の採点ルール（"score"）を読む
//   - 範囲外の子・循環など、評価すると落ちる木を読み込まない
//   - 読めるが意図どおりに動かない形を警告する
// ===========================================================================
#include "BehaviorTreeData.h"
#include <string>

class BehaviorTreeLoader
{
public:
    // JSON ファイルから BTAsset を読み込む。失敗したら false（outAsset の中身は使わないこと）
    static bool LoadFromJson(const std::string &path, BTAsset &outAsset);

private:
    // 文字列を BTNodeType に変換する。知らない文字列は Action として読む
    static BTNodeType StringToNodeType(const std::string &typeStr);

    // 範囲外の子・循環がないかを確かめる。false なら読み込みを中止する（path はログ用）
    static bool ValidateStructure(const BTAsset &asset, const std::string &path);

    // 読めるが意図どおりには動かない形を警告する。ValidateStructure を通った木に使う
    static void ValidateNodes(const BTAsset &asset, const std::string &path);

    // 1ノード分の行動・条件の ID を決める。保存キーを優先し、引けなければ数値 ID を使う（jNode は nlohmann::json*）
    static ActionID ResolveActionOrConditionId(const void *jNode, BTNodeType type,
                                               const std::string &path);

    // ノードの "score" を採点ルールにする。書かれていない項目は既定値（jScore は nlohmann::json*、path はログ用）
    static BTScoreRule ReadScoreRule(const void *jScore, const std::string &path);
};
