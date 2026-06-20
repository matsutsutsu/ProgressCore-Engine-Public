/// @file  StateTreeSerializer.h
/// @brief StateTreeDef の JSON 読み書きと、Def → 実行用 StateTreeAsset へのコンパイル。
#pragma once
#include <string>
#include <json.hpp>

#include "StateTreeDef.h"
#include "StateTreeAsset.h"

namespace CCL::AI
{
    class StateTreeSerializer
    {
    public:
        // --- JSON ⟷ Def ---

        /// @brief FileSystem 経由で .json を読み、StateTreeDef に変換する。
        /// @return 成功なら true
        static bool LoadDef(const std::string& virtualPath, StateTreeDef& out);

        /// @brief StateTreeDef を整形 JSON として FileSystem 経由で書き出す。
        static bool SaveDef(const std::string& virtualPath, const StateTreeDef& def);

        /// @brief json → StateTreeDef（メモリ内変換）
        static StateTreeDef FromJson(const nlohmann::json& j);

        /// @brief StateTreeDef → json（メモリ内変換）
        static nlohmann::json ToJson(const StateTreeDef& def);

        // --- Def → 実行用 Asset ---

        /// @brief 編集用 Def を実行用フラットアセットにコンパイルする。
        ///        type 名は NodeRegistry で解決、params は blob 化される。
        /// @note  NodeRegistry に種別が登録済みであること（RegisterNodes 済み）。
        static StateTreeAsset Compile(const StateTreeDef& def);
    };

} // namespace CCL::AI
