/// @file  NodeParamField.h
/// @brief タスク/条件の Params 構造体の「フィールド記述子」。
///
/// この1つの記述子が3か所を駆動する（DRY）:
///   1. JSON ローダ : json["speed"] → blob の offset へ型に応じて書く
///   2. JSON セーバ : エディタは json を直接編集するので記述子で UI を出すだけ
///   3. エディタ     : 各フィールドに対応する ImGui ウィジェットを自動生成
///
/// ComponentMeta::Fields() と同じ役割を StateTree ノードに持たせるもの。
#pragma once
#include <cstdint>
#include <cstddef> 
#include <string>
#include <vector>

namespace CCL::AI
{
    /// @brief paramsフィールドの型（M2 はスカラのみ。必要なら Vec3 等を追加）。
    enum class ParamType : uint8_t { Float, Int, Bool,String };

    /// @brief Params 構造体内の1フィールドの記述。
    struct NodeParamField
    {
        std::string name;            ///< JSONキー兼エディタ表示名（"cooldown" 等）
        ParamType   type = ParamType::Float;
        uint32_t    offset = 0;      ///< offsetof(Params, member)

        double defVal = 0.0;         ///< 既定値（json に無いときに使う）
        double minVal = 0.0;         ///< エディタ用レンジ下限（min==max のときクランプ無効）
        double maxVal = 0.0;         ///< エディタ用レンジ上限
        float  step   = 0.1f;        ///< DragFloat のステップ

         // String 型のときの char バッファ容量（sizeof(member)）
        uint32_t capacity = 0;
        // String 型の既定値（数値の defVal とは別に持つ）
        std::string defStr;

        /// @brief String 型がファイルパスのときの FileDialog プリセット contextId。
        ///        非空ならエディタが入力欄の隣に「参照」ボタンを出し、ダイアログで選べる。
        ///        空なら通常のテキスト入力（従来通り）。例: "BehaviorTreeNode"
        std::string presetContext;
    };
}

// ----------------------------------------------------------------------------
// 記述子を簡潔に書くためのマクロ。Params 構造体内で使う想定。
//   例: NODE_PARAM_FLOAT(Params, cooldown, "Cooldown", 1.2f, 0.0f, 10.0f)
// ----------------------------------------------------------------------------
#define NODE_PARAM_FLOAT(StructT, member, label, def, mn, mx)                 \
    CCL::AI::NodeParamField{ (label), CCL::AI::ParamType::Float,              \
        (uint32_t)offsetof(StructT, member), (double)(def),                  \
        (double)(mn), (double)(mx), 0.1f }

#define NODE_PARAM_INT(StructT, member, label, def, mn, mx)                   \
    CCL::AI::NodeParamField{ (label), CCL::AI::ParamType::Int,                \
        (uint32_t)offsetof(StructT, member), (double)(def),                  \
        (double)(mn), (double)(mx), 1.0f }

#define NODE_PARAM_BOOL(StructT, member, label, def)                          \
    CCL::AI::NodeParamField{ (label), CCL::AI::ParamType::Bool,               \
        (uint32_t)offsetof(StructT, member), (double)(def), 0.0, 0.0, 1.0f }

#define NODE_PARAM_STRING(StructT, member, label, defaultStr)                 \
    CCL::AI::NodeParamField{ (label), CCL::AI::ParamType::String,             \
        (uint32_t)offsetof(StructT, member), 0.0, 0.0, 0.0, 0.1f,             \
        (uint32_t)sizeof(((StructT*)0)->member), (defaultStr) }

// ファイルパス用の String フィールド。preset は FileDialog の contextId（"BehaviorTreeNode" 等）。
// エディタが入力欄の隣に「参照」ボタンを出し、ファイルダイアログから相対パスで選べるようになる。
#define NODE_PARAM_PATH(StructT, member, label, defaultStr, preset)            \
    CCL::AI::NodeParamField{ (label), CCL::AI::ParamType::String,             \
        (uint32_t)offsetof(StructT, member), 0.0, 0.0, 0.0, 0.1f,             \
        (uint32_t)sizeof(((StructT*)0)->member), (defaultStr), (preset) }
