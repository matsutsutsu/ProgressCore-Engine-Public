/**
 * @file BehaviorTreeLoader.h
 * @brief エディタが出力したJSONからランタイム用のBTAssetを構築する
 */
#pragma once
#include "BehaviorTreeData.h"
#include <string>


class BehaviorTreeLoader {
public:
    /**
     * @brief JSONファイルからBTAssetを読み込む
     */
    static bool LoadFromJson(const std::string& path, BTAsset& outAsset);

    /**
     * @brief ランタイムのBTAssetを人間が読める階層テキストに変換する
     */
    static std::string DumpTreeToString(const BTAsset& asset);

private:
    static BTNodeType StringToNodeType(const std::string& typeStr);


    // 再帰的にノードをテキスト化する内部関数
    static std::string DumpNodeRecursive(const BTAsset& asset, BTNodeID nodeId, const std::string& prefix, bool isLast);
};
