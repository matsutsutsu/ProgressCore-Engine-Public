// =====================================================================
// ComponentDefine.h
//   コンポーネントメタ定義の DSL。
//
//   フィールド宣言は CFIELD 系マクロに一本化 (旧 META_FIELD_* は撤去)。
//
//   使い分け:
//     ┌─────────────────────────────────┬───────────────────────────────────┐
//     │ シンプルなコンポーネント        │ COMPONENT_META_BEGIN/END で完結    │
//     │ (フィールドのみ・GUI 自動生成)  │ → CFIELD だけ書く                  │
//     ├─────────────────────────────────┼───────────────────────────────────┤
//     │ カスタム GUI が必要             │ ComponentMeta<T> を手書きする。    │
//     │ (Transform 等)                  │ 冒頭に CCL_META_USING_THIS(T) を   │
//     │                                 │ 書けば Fields() 内で CFIELD 可。   │
//     └─────────────────────────────────┴───────────────────────────────────┘
//
//   カスタムシリアライズが要る場合は ComponentMetaJson::ComponentSerializer<T>
//   を特殊化する (ComponentMetaJson.h を参照)。このマクロ群とは独立。
// =====================================================================
#pragma once
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#include <DirectXMath.h>
#include <string>
#include <type_traits>
#include <utility>

namespace ComponentMetaHelpers {

    // -----------------------------------------------------------------
    // フィールド型 → FieldDescriptor の自動マッピング。
    //   if constexpr で型ごとに分岐。未対応型はコンパイル時エラー。
    // -----------------------------------------------------------------
    template <typename ClassT, typename FieldT>
        constexpr FieldDescriptor MakeFieldAuto(
        const char* displayName,
        const char* jsonName,
        size_t      offset,
        const char* category,
        const char* tooltip  = nullptr,
        bool        readonly = false)
    {
        if constexpr (std::is_same_v<FieldT, float>)
        {
            return FD_Float(displayName, jsonName, offset, 0.05f, 0, 0, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, int>)
        {
            return FD_Int(displayName, jsonName, offset, 0, 0, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, bool>)
        {
            return FD_Bool(displayName, jsonName, offset, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, uint8_t>)
        {
            return FD_UInt8(displayName, jsonName, offset, 1.0f, 0, 0, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, uint16_t>)
        {
            return FD_UInt16(displayName, jsonName, offset, 1.0f, 0, 0, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, uint32_t>)
        {
            return FD_UInt32(displayName, jsonName, offset, 1.0f, 0, 0, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, DirectX::XMFLOAT2>)
        {
            return FD_Float2(displayName, jsonName, offset, 0.05f, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, DirectX::XMFLOAT3>)
        {
            return FD_Float3(displayName, jsonName, offset, 0.05f, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, DirectX::XMFLOAT4>)
        {
            return FD_Float4(displayName, jsonName, offset, 0.05f, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, std::string>)
        {
            return FD_String(displayName, jsonName, offset, category, true, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, CCL::Serialization::EntityLink>)
        {
            return FD_EntityLink(displayName, jsonName, offset, category, true, tooltip, readonly);
        }
        else {
            static_assert(!sizeof(FieldT*),
                "[CFIELD] Unsupported field type. Use CFIELD_ENUM_U8 / CFIELD_ENUM_INT / "
                "CFIELD_FLOAT_RANGED / CFIELD_INT_RANGED for special cases, "
                "extend MakeFieldAuto for new built-in types, or specialize "
                "ComponentMetaJson::ComponentSerializer<T> for fully custom data.");
        }
    }

} // namespace ComponentMetaHelpers


// =====================================================================
// CFIELD: 自動型推論で 1 フィールドを宣言する。
//   使い方: CFIELD(fieldName, "Display Name", "Category")
//   推論可能型: float / int / bool / uint8_t / uint16_t / uint32_t /
//               XMFLOAT2 / XMFLOAT3 / XMFLOAT4 / std::string / EntityLink
//   ※ ThisType に依存するため、COMPONENT_META_BEGIN か CCL_META_USING_THIS の
//     文脈でのみ使用できる。
// =====================================================================
// 基本形は従来通り: CFIELD(field, "表示名", "カテゴリ")
#define CFIELD(Field, Display, Category)                                                        \
    ComponentMetaHelpers::MakeFieldAuto<ThisType,                                               \
        std::remove_cv_t<std::remove_reference_t<decltype(std::declval<ThisType&>().Field)>>>(  \
        Display, #Field, offsetof(ThisType, Field), Category),

// ★ツールチップ付き: CFIELD_T(field, "表示名", "カテゴリ", "説明")
#define CFIELD_T(Field, Display, Category, Tooltip)                                             \
    ComponentMetaHelpers::MakeFieldAuto<ThisType,                                               \
        std::remove_cv_t<std::remove_reference_t<decltype(std::declval<ThisType&>().Field)>>>(  \
        Display, #Field, offsetof(ThisType, Field), Category, Tooltip),

// ★読み取り専用: CFIELD_RO(field, "表示名", "カテゴリ", "説明")  ※説明不要なら nullptr
#define CFIELD_RO(Field, Display, Category, Tooltip)                                            \
    ComponentMetaHelpers::MakeFieldAuto<ThisType,                                               \
        std::remove_cv_t<std::remove_reference_t<decltype(std::declval<ThisType&>().Field)>>>(  \
        Display, #Field, offsetof(ThisType, Field), Category, Tooltip, true),

// ★色 (XMFLOAT4 を ColorEdit4 で): CFIELD_COLOR(field, "表示名", "カテゴリ")
#define CFIELD_COLOR(Field, Display, Category)                                                  \
    FD_Color(Display, #Field, offsetof(ThisType, Field), Category),

// ★色＋説明
#define CFIELD_COLOR_T(Field, Display, Category, Tooltip)                                       \
    FD_Color(Display, #Field, offsetof(ThisType, Field), Category, true, Tooltip),

// --- 細かい制御が必要な特殊版 (デフォルト推論を上書き) -----------------------

/** @brief range / drag speed を指定する float */
#define CFIELD_FLOAT_RANGED(Field, Display, Speed, Min, Max, Category)                          \
    FD_Float(Display, #Field, offsetof(ThisType, Field), Speed, Min, Max, Category),

/** @brief range を指定する int */
#define CFIELD_INT_RANGED(Field, Display, Min, Max, Category)                                   \
    FD_Int(Display, #Field, offsetof(ThisType, Field), Min, Max, Category),

/** @brief uint8 ベースの enum (ImGui Combo に enum 名を出す) */
#define CFIELD_ENUM_U8(Field, Display, EnumNames, EnumCount, Category)                          \
    FD_EnumU8(Display, #Field, offsetof(ThisType, Field), EnumNames, EnumCount, Category),

/** @brief int ベースの enum */
#define CFIELD_ENUM_INT(Field, Display, EnumNames, EnumCount, Category)                         \
    FD_EnumInt(Display, #Field, offsetof(ThisType, Field), EnumNames, EnumCount, Category),

/** @brief drag speed を指定する Float3 (Position や Scale など) */
#define CFIELD_FLOAT3_FAST(Field, Display, Speed, Category)                                     \
    FD_Float3(Display, #Field, offsetof(ThisType, Field), Speed, Category),


// =====================================================================
// COMPONENT_META_BEGIN 系 / COMPONENT_META_END
//   シンプルなコンポーネントを 1 ブロックで完結させる。
//     1. ComponentMeta<T> の特殊化
//     2. Fields() の自動生成
//     3. REGISTER_COMPONENT (ランタイム登録)
//     4. REGISTER_COMPONENT_EDITOR_GUI (エディタのみ)
//
//   3 種類の入口 (どれも END で閉じる):
//     COMPONENT_META_BEGIN(T, "表示名")
//         → カテゴリ "Misc" / 並び順 1000 / 非表示なし
//     COMPONENT_META_BEGIN_CAT(T, "表示名", "カテゴリ")
//         → カテゴリ指定 / 並び順 1000 / 非表示なし
//     COMPONENT_META_BEGIN_FULL(T, "表示名", "カテゴリ", 並び順, 非表示)
//         → 全指定 (並び順は小さいほどインスペクタ上段)
//
//   ※ 同じ T を別の翻訳単位で特殊化しないこと (ODR)。
//     通常は 1 コンポーネント = 1 cpp なので問題ない。
// =====================================================================

// --- 基本形: カテゴリ・並び順・非表示まで全指定 (他の BEGIN はこれに委譲) ---
#define COMPONENT_META_BEGIN_FULL(T, NameStr, CategoryStr, Priority, Hidden)                    \
    template <>                                                                                 \
    struct ComponentMeta<T>                                                                     \
    {                                                                                           \
        using ThisType                                 = T;                                     \
        static constexpr const char* displayName       = NameStr;                               \
        static constexpr const char* category          = CategoryStr;                           \
        static constexpr int         sortPriority      = Priority;                              \
        static constexpr bool        hiddenInInspector = Hidden;                                \
        static constexpr bool        hasCustomGui      = false;                                 \
        static constexpr bool        isSerializable    = true;                                  \
        static bool CustomGui(T&, unsigned long long = 0, void* = nullptr) { return false; }    \
        static const std::vector<FieldDescriptor>& Fields()                                     \
        {                                                                                       \
            static const std::vector<FieldDescriptor> fields = {

// --- カテゴリ指定版: 並び順は既定(1000)、非表示なし ---
#define COMPONENT_META_BEGIN_CAT(T, NameStr, CategoryStr)                                        \
    COMPONENT_META_BEGIN_FULL(T, NameStr, CategoryStr, 1000, false)

// --- 無印版: カテゴリ "Misc"、並び順は既定(1000)、非表示なし ---
#define COMPONENT_META_BEGIN(T, NameStr)                                                         \
    COMPONENT_META_BEGIN_FULL(T, NameStr, "Misc", 1000, false)

// --- 終端: すべての BEGIN 系はこれで閉じる ---
#define COMPONENT_META_END(T)                                                                    \
            };                                                                                   \
            return fields;                                                                       \
        }                                                                                        \
    };                                                                                           \
    REGISTER_COMPONENT(T, ComponentMeta<T>::displayName)                                         \
    REGISTER_COMPONENT_EDITOR_GUI(T)


// =====================================================================
// CCL_META_USING_THIS
//   手書きの ComponentMeta<T> 特殊化の中で CFIELD を使えるようにする。
//   ComponentMeta<T> の冒頭に書く。
// =====================================================================
#define CCL_META_USING_THIS(T)  using ThisType = T;