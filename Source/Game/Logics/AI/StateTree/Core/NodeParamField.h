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
#include <cstddef>   // offsetof
#include <string>
#include <vector>

namespace CCL::AI
{
    /// @brief paramsフィールドの型（M2 はスカラのみ。必要なら Vec3 等を追加）。
    enum class ParamType : uint8_t { Float, Int, Bool };

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
