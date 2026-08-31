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

    /// @brief 1ノードぶんの行動/条件を決める。キー文字列が正で、数値IDは旧アセット互換。
    /// @param jNode  ノードのJSON（nlohmann::json だがヘッダに持ち込みたくないので void*）
    /// @note  未知のキーは0（None）にせず、数値の方を採る。名前を打ち間違えただけで
    ///        木が丸ごと無反応になるより、旧挙動のまま動いて警告が出る方が気づける。
    static ActionID ResolveActionOrConditionId(const void* jNode, BTNodeType type,
                                               const std::string& path);

    /// @brief 待ち時間がActionIDに埋まっていた頃（A_Wait0_5s〜A_Wait10_0s）のデータを
    ///        「A_Wait ＋ param（秒）」へ読み替える。
    /// @note  リダイレクタ。アセットを全部移行済みでも、手元のバックアップや
    ///        作業中のブランチから古い木を開いたときに黙って壊れないよう残す。
    static void MigrateLegacyWait(BTNode& node);

    /// @brief 間合いがActionIDに埋まっていた頃の条件（C_RangeClose 等）を
    ///        「C_DistanceLessThan/GreaterThan + param(距離)」へ読み替える。
    /// @param savedKey アセットに書かれていた "condition" 文字列（無ければ空でよい）
    /// @note  対応表は ActionRegistry.h の g_LegacyConditions。読み替えたことはログに出す。
    ///        意味を完全には復元できないもの（帯だった C_RangeMedium）は警告にする。
    static void MigrateLegacyCondition(BTNode& node, const std::string& savedKey,
                                       const std::string& path);

    // 再帰的にノードをテキスト化する内部関数
    static std::string DumpNodeRecursive(const BTAsset& asset, BTNodeID nodeId, const std::string& prefix, bool isLast);
};
