#pragma once
// ===========================================================================
// 概要: コンポーネントのメタ定義を書くためのマクロ群（CFIELD / COMPONENT_META_BEGIN 等）
//
// 責務:
//   - フィールドの型から FieldDescriptor を自動で作る（CFIELD 系）
//   - ComponentMeta<T> の特殊化と登録を 1 ブロックで書けるようにする（COMPONENT_META_BEGIN 系）
//
// 備考: 使い分け
//   - フィールドだけのコンポーネント: COMPONENT_META_BEGIN 〜 END の中に CFIELD を並べる
//   - 独自の GUI が要るコンポーネント: ComponentMeta<T> を手書きし、冒頭に
//     CCL_META_USING_THIS(T) を書けば Fields() の中で CFIELD が使える
//   - 保存形式そのものを変えたい場合は ComponentMetaJson::ComponentSerializer<T> を特殊化する
// ===========================================================================
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#include <DirectXMath.h>
#include <string>
#include <type_traits>
#include <utility>

namespace ComponentMetaHelpers
{

    // フィールドの型から FieldDescriptor を作る。if constexpr で型ごとに分け、未対応の型はコンパイルエラーにする
    template <typename ClassT, typename FieldT>
    constexpr FieldDescriptor MakeFieldAuto(
        const char *displayName,
        const char *jsonName,
        size_t      offset,
        const char *category,
        const char *tooltip   = nullptr,
        bool        readonly  = false,
        bool        serialize = true)
    {
        if constexpr (std::is_same_v<FieldT, float>)
        {
            return FD_Float(displayName, jsonName, offset, 0.05f, 0, 0, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, int>)
        {
            return FD_Int(displayName, jsonName, offset, 0, 0, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, bool>)
        {
            return FD_Bool(displayName, jsonName, offset, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, uint8_t>)
        {
            return FD_UInt8(displayName, jsonName, offset, 1.0f, 0, 0, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, uint16_t>)
        {
            return FD_UInt16(displayName, jsonName, offset, 1.0f, 0, 0, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, uint32_t>)
        {
            return FD_UInt32(displayName, jsonName, offset, 1.0f, 0, 0, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, DirectX::XMFLOAT2>)
        {
            return FD_Float2(displayName, jsonName, offset, 0.05f, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, DirectX::XMFLOAT3>)
        {
            return FD_Float3(displayName, jsonName, offset, 0.05f, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, DirectX::XMFLOAT4>)
        {
            return FD_Float4(displayName, jsonName, offset, 0.05f, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, std::string>)
        {
            return FD_String(displayName, jsonName, offset, category, serialize, tooltip, readonly);
        }
        else if constexpr (std::is_same_v<FieldT, CCL::Serialization::EntityLink>)
        {
            return FD_EntityLink(displayName, jsonName, offset, category, serialize, tooltip, readonly);
        }
        else
        {
            static_assert(!sizeof(FieldT *),
                          "[CFIELD] Unsupported field type. Use CFIELD_ENUM_U8 / CFIELD_ENUM_INT / "
                          "CFIELD_FLOAT_RANGED / CFIELD_INT_RANGED for special cases, "
                          "extend MakeFieldAuto for new built-in types, or specialize "
                          "ComponentMetaJson::ComponentSerializer<T> for fully custom data.");
        }
    }

} // namespace ComponentMetaHelpers

// ===========================================================================
// CFIELD 系: フィールドを 1 つ宣言する
//   型から自動で決まる: float / int / bool / uint8_t / uint16_t / uint32_t /
//                       XMFLOAT2 / XMFLOAT3 / XMFLOAT4 / std::string / EntityLink
//   ThisType を使うので、COMPONENT_META_BEGIN か CCL_META_USING_THIS の中でだけ使える
// ===========================================================================

// 基本形: CFIELD(field, "表示名", "カテゴリ")
#define CFIELD(Field, Display, Category)                                                                                        \
    ComponentMetaHelpers::MakeFieldAuto<ThisType,                                                                               \
                                        std::remove_cv_t<std::remove_reference_t<decltype(std::declval<ThisType &>().Field)>>>( \
        Display, #Field, offsetof(ThisType, Field), Category),

// ツールチップ付き: CFIELD_T(field, "表示名", "カテゴリ", "説明")
#define CFIELD_T(Field, Display, Category, Tooltip)                                                                             \
    ComponentMetaHelpers::MakeFieldAuto<ThisType,                                                                               \
                                        std::remove_cv_t<std::remove_reference_t<decltype(std::declval<ThisType &>().Field)>>>( \
        Display, #Field, offsetof(ThisType, Field), Category, Tooltip),

// 読み取り専用: CFIELD_RO(field, "表示名", "カテゴリ", "説明")。説明が不要なら nullptr
#define CFIELD_RO(Field, Display, Category, Tooltip)                                                                            \
    ComponentMetaHelpers::MakeFieldAuto<ThisType,                                                                               \
                                        std::remove_cv_t<std::remove_reference_t<decltype(std::declval<ThisType &>().Field)>>>( \
        Display, #Field, offsetof(ThisType, Field), Category, Tooltip, true),

// CFIELD_*_RT: 実行時専用のフィールド。インスペクタには出すが保存しない
//   System が書くフィールド（現在のステート・タイマー・計算済みの目標など）を保存すると、
//   保存した瞬間の値がプレハブに残り、そこから生成した Entity がその値で始まってしまう
//   今の値を見られること自体はデバッグに要るので、表示は残して保存だけを止める
#define CFIELD_RT(Field, Display, Category)                                                                                     \
    ComponentMetaHelpers::MakeFieldAuto<ThisType,                                                                               \
                                        std::remove_cv_t<std::remove_reference_t<decltype(std::declval<ThisType &>().Field)>>>( \
        Display, #Field, offsetof(ThisType, Field), Category, nullptr, false, false),

// uint8 の enum（実行時専用）
#define CFIELD_ENUM_U8_RT(Field, Display, EnumNames, EnumCount, Category) \
    FD_EnumU8(Display, #Field, offsetof(ThisType, Field), EnumNames,      \
              static_cast<int>(EnumCount), Category, false),

// Float3（実行時専用）
#define CFIELD_FLOAT3_RT(Field, Display, Speed, Category) \
    FD_Float3(Display, #Field, offsetof(ThisType, Field), Speed, Category, false),

// 色（XMFLOAT4 をカラーピッカーで編集）: CFIELD_COLOR(field, "表示名", "カテゴリ")
#define CFIELD_COLOR(Field, Display, Category) \
    FD_Color(Display, #Field, offsetof(ThisType, Field), Category),

// ファイルパス（std::string）をテキスト入力とファイルダイアログで編集する
//   Preset は FileDialog のプリセット名（例 "Texture"）。説明が不要なら nullptr
//   CFIELD_ASSET(field, "表示名", "カテゴリ", "Texture", "説明")
#define CFIELD_ASSET(Field, Display, Category, Preset, Tooltip) \
    FD_String(Display, #Field, offsetof(ThisType, Field), Category, true, Tooltip, false, Preset),

// 以下は型からの自動推論を使わず、範囲などを指定する版

// 範囲とドラッグ速度を指定する float
#define CFIELD_FLOAT_RANGED(Field, Display, Speed, Min, Max, Category) \
    FD_Float(Display, #Field, offsetof(ThisType, Field), Speed, Min, Max, Category),

// 範囲を指定する int
#define CFIELD_INT_RANGED(Field, Display, Min, Max, Category) \
    FD_Int(Display, #Field, offsetof(ThisType, Field), Min, Max, Category),

// uint8 の enum（コンボボックスに enum 名を出す）
//   EnumCount には std::size(...)（size_t）を渡せるよう、ここで int にキャストする
#define CFIELD_ENUM_U8(Field, Display, EnumNames, EnumCount, Category) \
    FD_EnumU8(Display, #Field, offsetof(ThisType, Field), EnumNames, static_cast<int>(EnumCount), Category),

// int の enum
#define CFIELD_ENUM_INT(Field, Display, EnumNames, EnumCount, Category) \
    FD_EnumInt(Display, #Field, offsetof(ThisType, Field), EnumNames, static_cast<int>(EnumCount), Category),

// ドラッグ速度を指定する Float3（位置やスケールなど）
#define CFIELD_FLOAT3_FAST(Field, Display, Speed, Category) \
    FD_Float3(Display, #Field, offsetof(ThisType, Field), Speed, Category),

// ===========================================================================
// COMPONENT_META_BEGIN 系 〜 COMPONENT_META_END
//   ComponentMeta<T> の特殊化・Fields()・REGISTER_COMPONENT・
//   REGISTER_COMPONENT_EDITOR_GUI を 1 ブロックで書く。入口は 3 種類（どれも END で閉じる）:
//     COMPONENT_META_BEGIN(T, "表示名")                               カテゴリ Misc / 並び順 1000
//     COMPONENT_META_BEGIN_CAT(T, "表示名", "カテゴリ")               並び順 1000
//     COMPONENT_META_BEGIN_FULL(T, "表示名", "カテゴリ", 並び順, 非表示) すべて指定（並び順は小さいほど上段）
//   同じ T を別の翻訳単位で特殊化しないこと（ODR 違反になる）
// ===========================================================================

// すべて指定する版（他の BEGIN はこれを呼ぶ）
#define COMPONENT_META_BEGIN_FULL(T, NameStr, CategoryStr, Priority, Hidden)                                                  \
    template <>                                                                                                               \
    struct ComponentMeta<T>                                                                                                   \
    {                                                                                                                         \
        using ThisType                                               = T;                                                     \
        static constexpr const char               *displayName       = NameStr;                                               \
        static constexpr const char               *category          = CategoryStr;                                           \
        static constexpr int                       sortPriority      = Priority;                                              \
        static constexpr bool                      hiddenInInspector = Hidden;                                                \
        static constexpr bool                      hasCustomGui      = false;                                                 \
        static constexpr bool                      isSerializable    = true;                                                  \
        static bool                                CustomGui(T &, unsigned long long = 0, void * = nullptr) { return false; } \
        static const std::vector<FieldDescriptor> &Fields()                                                                   \
        {                                                                                                                     \
            static const std::vector<FieldDescriptor> fields = {

// カテゴリだけ指定する版
#define COMPONENT_META_BEGIN_CAT(T, NameStr, CategoryStr) \
    COMPONENT_META_BEGIN_FULL(T, NameStr, CategoryStr, 1000, false)

// 表示名だけ指定する版
#define COMPONENT_META_BEGIN(T, NameStr) \
    COMPONENT_META_BEGIN_FULL(T, NameStr, "Misc", 1000, false)

// BEGIN 系を閉じる
#define COMPONENT_META_END(T)                            \
    }                                                    \
    ;                                                    \
    return fields;                                       \
    }                                                    \
    }                                                    \
    ;                                                    \
    REGISTER_COMPONENT(T, ComponentMeta<T>::displayName) \
    REGISTER_COMPONENT_EDITOR_GUI(T)

// 手書きの ComponentMeta<T> の中で CFIELD を使えるようにする（ComponentMeta<T> の冒頭に書く）
#define CCL_META_USING_THIS(T) using ThisType = T;