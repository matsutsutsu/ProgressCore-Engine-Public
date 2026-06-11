// =====================================================================
// ComponentMeta.h
//   コンポーネントの「フィールド記述子 (FieldDescriptor)」と
//   メタデータテンプレート (ComponentMeta<T>) の定義。
//
//   リファクタリング方針:
//     - 旧 FieldKind::EntityID / FD_EntityID は完全撤去 (EntityLink に一本化)。
//     - FD_* ビルダーの記法を全種類で統一 (旧 TODO: 文法バラバラ問題を解消)。
//     - ComponentMeta<T> から未使用メンバー (registered / version /
//       hasCustomSerialize) を撤去。役割のあるメンバーだけ残す。
//     - 旧 META_FIELD_* マクロは撤去。フィールド宣言は CFIELD 系に一本化
//       (ComponentDefine.h)。
// =====================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------
// フィールド種別
//   ※ EntityID は撤去。エンティティ参照は EntityLink のみ。
// ---------------------------------------------------------------------
enum class FieldKind : uint8_t {
    Float,
    Int,
    Bool,
    UInt8,
    UInt16,
    UInt32,
    EntityLink,   // エンティティ参照。保存時にローカルインデックスへ自動変換される。
    EnumU8,
    EnumInt,
    Float2,
    Float3,
    Float4,
    String
};

// ---------------------------------------------------------------------
// フィールド 1 つ分の記述子。
//   メンバー順は FD_* ビルダーの brace 初期化順と一致させること。
// ---------------------------------------------------------------------
struct FieldDescriptor {
    const char* name = nullptr;            // インスペクタ表示名
    const char* jsonName = nullptr;            // JSON 保存キー
    FieldKind          kind = FieldKind::Float;
    size_t             offset = 0;                  // 構造体先頭からのバイトオフセット
    float              dragSpeed = 0.05f;
    float              rangeMin = 0.0f;
    float              rangeMax = 0.0f;
    const char* category = nullptr;            // インスペクタの折りたたみ見出し
    const char* const* enumNames = nullptr;            // enum 系のみ使用
    int                enumCount = 0;                  // enum 系のみ使用
    bool               serialize = true;               // false なら JSON に書き出さない
};

// ---------------------------------------------------------------------
// FieldDescriptor ビルダー (全種類で同一記法に統一)
//   引数順: 表示名, JSONキー, offset, (種別ごとの追加引数...), category, serialize
//   返り値: FieldDescriptor のメンバー順そのままの brace 初期化。
// ---------------------------------------------------------------------
inline FieldDescriptor FD_Float(const char* name, const char* jsonName, size_t offset,
    float speed = 0.05f, float rmin = 0.0f, float rmax = 0.0f,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::Float, offset, speed, rmin, rmax, category, nullptr, 0, serialize };
}
inline FieldDescriptor FD_Int(const char* name, const char* jsonName, size_t offset,
    float rmin = 0.0f, float rmax = 0.0f,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::Int, offset, 0.05f, rmin, rmax, category, nullptr, 0, serialize };
}
inline FieldDescriptor FD_Bool(const char* name, const char* jsonName, size_t offset,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::Bool, offset, 0.05f, 0.0f, 0.0f, category, nullptr, 0, serialize };
}
inline FieldDescriptor FD_UInt8(const char* name, const char* jsonName, size_t offset,
    float speed = 1.0f, float rmin = 0.0f, float rmax = 0.0f,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::UInt8, offset, speed, rmin, rmax, category, nullptr, 0, serialize };
}
inline FieldDescriptor FD_UInt16(const char* name, const char* jsonName, size_t offset,
    float speed = 1.0f, float rmin = 0.0f, float rmax = 0.0f,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::UInt16, offset, speed, rmin, rmax, category, nullptr, 0, serialize };
}
inline FieldDescriptor FD_UInt32(const char* name, const char* jsonName, size_t offset,
    float speed = 1.0f, float rmin = 0.0f, float rmax = 0.0f,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::UInt32, offset, speed, rmin, rmax, category, nullptr, 0, serialize };
}
// エンティティ参照。SaveContext/LoadContext で自動 remap される。
inline FieldDescriptor FD_EntityLink(const char* name, const char* jsonName, size_t offset,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::EntityLink, offset, 0.0f, 0.0f, 0.0f, category, nullptr, 0, serialize };
}
inline FieldDescriptor FD_EnumU8(const char* name, const char* jsonName, size_t offset,
    const char* const* enumNames, int enumCount,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::EnumU8, offset, 0.0f, 0.0f, 0.0f, category, enumNames, enumCount, serialize };
}
inline FieldDescriptor FD_EnumInt(const char* name, const char* jsonName, size_t offset,
    const char* const* enumNames, int enumCount,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::EnumInt, offset, 0.0f, 0.0f, 0.0f, category, enumNames, enumCount, serialize };
}
inline FieldDescriptor FD_Float2(const char* name, const char* jsonName, size_t offset,
    float speed = 0.05f, const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::Float2, offset, speed, 0.0f, 0.0f, category, nullptr, 0, serialize };
}
inline FieldDescriptor FD_Float3(const char* name, const char* jsonName, size_t offset,
    float speed = 0.05f, const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::Float3, offset, speed, 0.0f, 0.0f, category, nullptr, 0, serialize };
}
inline FieldDescriptor FD_Float4(const char* name, const char* jsonName, size_t offset,
    float speed = 0.05f, const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::Float4, offset, speed, 0.0f, 0.0f, category, nullptr, 0, serialize };
}
inline FieldDescriptor FD_String(const char* name, const char* jsonName, size_t offset,
    const char* category = nullptr, bool serialize = true) {
    return { name, jsonName, FieldKind::String, offset, 0.0f, 0.0f, 0.0f, category, nullptr, 0, serialize };
}


// =====================================================================
// ComponentMeta<T>  : コンポーネント型ごとのメタデータ
//
//   役割を持つメンバーだけを残した最小構成:
//     displayName    : インスペクタ / JSON 上の名前
//     hasCustomGui   : true なら CustomGui() を使って手描画 (ComponentMetaImGui)
//     isSerializable : false ならシーン保存対象外 (ComponentRegistry)
//     CustomGui()    : hasCustomGui = true のときの描画関数
//     Fields()       : シリアライズ & 自動インスペクタの対象フィールド一覧
//
//   ★ シリアライズのカスタマイズは ComponentMeta ではなく
//     ComponentMetaJson::ComponentSerializer<T> を特殊化して行う
//     (ComponentMetaJson.h)。ここには serialize 系メンバーを持たせない。
// =====================================================================
template <typename T>
struct ComponentMeta {
    static constexpr const char* displayName = "(unregistered)";
    static constexpr bool        hasCustomGui = false;
    static constexpr bool        isSerializable = true;

    // 戻り値: 値が変更されたら true (Undo/Redo フラグ用)。
    static bool CustomGui(T& /*comp*/, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        return false;
    }

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};


// =====================================================================
// RawField : 「先頭アドレス + offset」を任意型として読む低レベルヘルパー。
//   FieldDescriptor.offset を使ってメンバーへ型安全にアクセスするために使う。
// =====================================================================
template <typename FieldT>
inline FieldT& RawField(void* base, size_t offset) {
    return *reinterpret_cast<FieldT*>(static_cast<char*>(base) + offset);
}
template <typename FieldT>
inline const FieldT& RawField(const void* base, size_t offset) {
    return *reinterpret_cast<const FieldT*>(static_cast<const char*>(base) + offset);
}