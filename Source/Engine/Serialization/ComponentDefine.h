// =====================================================================
// ComponentDefine.h
//   コンポーネントメタ定義の DSL (Domain-Specific Language)。
//
//   目的:
//     - 「コンポーネントを追加する」作業を 1 ファイル数行で完結させる
//     - 型を毎回手書きする (META_FIELD_FLOAT3 等) のをやめる
//     - decltype による自動型推論で書き間違いを排除する
//
//   使い分け:
//     ┌─────────────────────────────────┬───────────────────────────────────┐
//     │ シンプルなコンポーネント        │ COMPONENT_META_BEGIN/END で完結    │
//     │ (フィールドのみ・GUI 自動生成)  │ → CFIELD だけ書く                  │
//     ├─────────────────────────────────┼───────────────────────────────────┤
//     │ カスタム GUI が必要             │ ComponentMeta<T> を手書きするが    │
//     │ (Transform, Model 等)           │ Fields() 内では CFIELD を使える    │
//     └─────────────────────────────────┴───────────────────────────────────┘
//
//   使用例 (シンプル):
//
//     #include "MyComponent.h"
//     #include "Engine/Serialization/Meta/ComponentDefine.h"
//
//     COMPONENT_META_BEGIN(MyComponent, "MyComponent")
//         CFIELD(speed,    "Speed",    "Movement")
//         CFIELD(position, "Position", "Movement")
//         CFIELD(target,   "Target",   "Combat")
//     COMPONENT_META_END(MyComponent)
//
//   これで:
//     - JSON シリアライズ
//     - ImGui インスペクタ表示
//     - ComponentRegistry への自動登録
//     - エディタ ↔ ランタイム分離
//   すべてが自動で組み立てられる。
// =====================================================================
#pragma once
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/EntityLink.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/ComponentRegistry_Editor.h"
#include <DirectXMath.h>
#include <string>
#include <type_traits>
#include <utility>

namespace ComponentMetaHelpers {

    // -----------------------------------------------------------------
    // フィールド型 → FieldDescriptor の自動マッピング
    //
    //   if constexpr で型ごとに分岐し、適切な FD_* を返す。
    //   未対応型はコンパイル時に明示エラーにする (静的アサート)。
    // -----------------------------------------------------------------
    template <typename ClassT, typename FieldT>
    constexpr FieldDescriptor MakeFieldAuto(
        const char* displayName,
        const char* jsonName,
        size_t      offset,
        const char* category)
    {
        if constexpr (std::is_same_v<FieldT, float>) {
            return FD_Float(displayName, jsonName, offset, 0.05f, 0, 0, category);
        }
        else if constexpr (std::is_same_v<FieldT, int>) {
            return FD_Int(displayName, jsonName, offset, 0, 0, category);
        }
        else if constexpr (std::is_same_v<FieldT, bool>) {
            return FD_Bool(displayName, jsonName, offset, category);
        }
        else if constexpr (std::is_same_v<FieldT, uint8_t>) {
            return FD_UInt8(displayName, jsonName, offset, 1.0f, 0, 0, category);
        }
        else if constexpr (std::is_same_v<FieldT, uint16_t>) {
            return FD_UInt16(displayName, jsonName, offset, 1.0f, 0, 0, category);
        }
        else if constexpr (std::is_same_v<FieldT, uint32_t>) {
            return FD_UInt32(displayName, jsonName, offset, 1.0f, 0, 0, category);
        }
        else if constexpr (std::is_same_v<FieldT, DirectX::XMFLOAT2>) {
            return FD_Float2(displayName, jsonName, offset, 0.05f, category);
        }
        else if constexpr (std::is_same_v<FieldT, DirectX::XMFLOAT3>) {
            return FD_Float3(displayName, jsonName, offset, 0.05f, category);
        }
        else if constexpr (std::is_same_v<FieldT, DirectX::XMFLOAT4>) {
            return FD_Float4(displayName, jsonName, offset, 0.05f, category);
        }
        else if constexpr (std::is_same_v<FieldT, std::string>) {
            return FD_String(displayName, jsonName, offset, category);
        }
        else if constexpr (std::is_same_v<FieldT, CCL::Serialization::EntityLink>) {
            return FD_EntityLink(displayName, jsonName, offset, category);
        }
        else {
            // 未対応型: 明示的なマクロ (CFIELD_ENUM_U8 等) を使うよう指示する
            static_assert(!sizeof(FieldT*),
                "[CFIELD] Unsupported field type. Use CFIELD_ENUM_U8 / CFIELD_ENUM_INT / "
                "CFIELD_FLOAT_RANGED / CFIELD_INT_RANGED for special cases, "
                "or extend MakeFieldAuto for new built-in types.");
        }
    }

} // namespace ComponentMetaHelpers


// =====================================================================
// ★ メインの DSL マクロ
// =====================================================================

/**
 * @brief 自動型推論で 1 フィールドを宣言する。
 *
 * 使い方: `CFIELD(fieldName, "Display Name", "Category")`
 *
 * 推論可能な型:
 *   float / int / bool / uint8_t / uint16_t / uint32_t /
 *   XMFLOAT2 / XMFLOAT3 / XMFLOAT4 / std::string / EntityLink
 *
 * 推論できない型 (enum 等) は CFIELD_ENUM_U8 などの専用マクロを使う。
 */
#define CFIELD(Field, Display, Category)                                                        \
    ComponentMetaHelpers::MakeFieldAuto<ThisType,                                               \
        std::remove_cv_t<std::remove_reference_t<decltype(std::declval<ThisType&>().Field)>>>(  \
        Display, #Field, offsetof(ThisType, Field), Category),

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

/** @brief 旧形式の生 EntityID (新規には使わない。移行期の互換用) */
#define CFIELD_ENTITY_ID(Field, Display, Category)                                              \
    FD_EntityID(Display, #Field, offsetof(ThisType, Field), Category),


// =====================================================================
// シンプルなコンポーネント定義の BEGIN/END
//
// 1 ブロックで以下をすべて行う:
//   1. ComponentMeta<T> の特殊化
//   2. Fields() の自動生成
//   3. REGISTER_COMPONENT (ランタイム)
//   4. REGISTER_COMPONENT_EDITOR_GUI (エディタのみ)
//
//   注意: COMPONENT_META_BEGIN は ComponentMeta<T> を「特殊化」するため、
//         同じ T を別の翻訳単位で特殊化しないこと (ODR 違反)。
//         通常は 1 コンポーネント = 1 cpp ファイルなので問題ない。
// =====================================================================

/**
 * @brief シンプルなコンポーネントメタ定義の開始。
 *
 * @param T       コンポーネント型
 * @param NameStr エディタ / JSON に出る表示名 (文字列リテラル)
 *
 * 続けて CFIELD(...) を必要数並べ、最後に COMPONENT_META_END(T) で閉じる。
 *
 * カスタム GUI が必要な場合はこのマクロを使わず、ComponentMeta<T> を
 * 手書きすること (ただし Fields() 内では CFIELD を使える)。
 */
#define COMPONENT_META_BEGIN(T, NameStr)                                                        \
    template <> struct ComponentMeta<T> {                                                       \
        using ThisType = T;                                                                     \
        static constexpr bool        registered     = true;                                     \
        static constexpr const char* displayName    = NameStr;                                  \
        static constexpr bool        hasCustomGui   = false;                                    \
        static constexpr bool        isSerializable = true;                                     \
        static bool CustomGui(T&, unsigned long long = 0, void* = nullptr) { return false; }   \
        static const std::vector<FieldDescriptor>& Fields() {                                   \
            static const std::vector<FieldDescriptor> fields = {

 /**
  * @brief コンポーネントメタ定義の終了 + 自動登録。
  *
  * @param T コンポーネント型 (BEGIN と同じ型を渡す)
  */
 #define COMPONENT_META_END(T)                                                                   \
            };                                                                                  \
            return fields;                                                                      \
        }                                                                                       \
    };                                                                                          \
    REGISTER_COMPONENT(T, ComponentMeta<T>::displayName)                                        \
    REGISTER_COMPONENT_EDITOR_GUI(T)


  // =====================================================================
  // カスタム GUI を持つコンポーネントを「手書き」する場合のヘルパー
  //
  //   ComponentMeta<T> を自分で書くが、Fields() 内では CFIELD を使えるように
  //   ThisType エイリアスを宣言するヘルパー。
  //
  //   使い方:
  //
  //     template <> struct ComponentMeta<TransformComponent> {
  //         CCL_META_USING_THIS(TransformComponent)        // ← これを書くと CFIELD が使える
  //         static constexpr const char* displayName = "Transform";
  //         static constexpr bool hasCustomGui = true;
  //
  //         static bool CustomGui(TransformComponent& comp, ...) {
  //             // 自作 GUI
  //         }
  //
  //         static const std::vector<FieldDescriptor>& Fields() {
  //             static const std::vector<FieldDescriptor> fields = {
  //                 CFIELD(position, "Position", "Transform")    // ← CFIELD が使える
  //                 CFIELD(rotation, "Rotation", "Transform")
  //                 CFIELD(scale,    "Scale",    "Transform")
  //                 CFIELD(parent,   "Parent",   "Hierarchy")
  //             };
  //             return fields;
  //         }
  //     };
  //     REGISTER_COMPONENT(TransformComponent, "Transform")
  //     REGISTER_COMPONENT_EDITOR_GUI(TransformComponent)
  // =====================================================================

  /** @brief CFIELD マクロが参照する ThisType エイリアスを宣言する */
#define CCL_META_USING_THIS(T)                          \
    using ThisType = T;                                 \
    static constexpr bool registered = true;