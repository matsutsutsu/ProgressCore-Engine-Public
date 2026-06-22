/// @file  StateTreeDef.h
/// @brief StateTree の「編集用・JSON 1:1」のネスト表現。
/// 　　　 編集しやすい形（Def）と実行が早い形（Assets）を行き来する
///
/// 実行用 StateTreeAsset（フラット配列・blob）とは別物。
///   - エディタはこの StateTreeDef を編集する
///   - JSON とは構造が 1:1（そのまま読み書き）
///   - params は nlohmann::json のまま保持（blob 化は Compile 時のみ）
///
/// この分離により、エディタは型情報やオフセットを気にせず json を直接いじれる。
#pragma once
#include <string>
#include <vector>
#include <json.hpp>   // パスは環境に合わせて調整してください

namespace CCL::AI
{
    /// @brief タスク or 条件1個の定義。
    struct NodeDef
    {
        std::string    type;                              ///< "MoveToTarget" 等（NodeRegistry の名前）
        nlohmann::json params = nlohmann::json::object(); ///< {"speed": 3.5} 等。空でも可
    };

    /// @brief 状態1個の定義（子を持てる ＝ 木構造）。
    struct StateDef
    {
        std::string           name = "State";
        std::vector<NodeDef>  enterConditions;  ///< この状態に入れる条件（全 true で選択可）
        std::vector<NodeDef>  tasks;            ///< 滞在中に実行するタスク
        std::vector<StateDef> children;         ///< 子状態
    };

    /// @brief 1本の StateTree 全体の定義（= 1つの .json）。
    struct StateTreeDef
    {
        std::string name = "Untitled";
        StateDef    root;   ///< root.name は慣例上 "Root"
    };

} // namespace CCL::AI
