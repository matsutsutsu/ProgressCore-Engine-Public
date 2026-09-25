#pragma once
// ===========================================================================
// 概要: コンポーネントのフィールド記述子（FieldDescriptor）とメタデータ（ComponentMeta<T>）の定義。
//
// 責務:
//   - フィールド 1 つ分の情報（名前・型・オフセット・表示設定・保存するか）を表す
//   - コンポーネント型ごとの表示名・カテゴリ・フィールド一覧を ComponentMeta<T> で表す
//   - FieldDescriptor を作る FD_* 関数と、オフセットでフィールドを読む RawField
//
// 備考: 定義は直接書かず、ComponentDefine.h の CFIELD / COMPONENT_META_BEGIN 系マクロで書く。
// ===========================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ===========================================================================
// FieldDescriptor
// ===========================================================================

// フィールドの種別
enum class FieldKind : uint8_t
{
    Float,
    Int,
    Bool,
    UInt8,
    UInt16,
    UInt32,
    EntityLink, // Entity への参照。保存時はローカルインデックスに変換される
    EnumU8,
    EnumInt,
    Float2,
    Float3,
    Float4,
    Color,
    String
};

// フィールド 1 つ分の記述子。メンバーの順は FD_* 関数の波括弧初期化の順と一致させること
struct FieldDescriptor
{
    const char        *name       = nullptr; // インスペクタ表示名
    const char        *jsonName   = nullptr; // JSON 保存キー
    FieldKind          kind       = FieldKind::Float;
    size_t             offset     = 0;       // 構造体の先頭からのバイトオフセット
    float              dragSpeed  = 0.05f;   // インスペクタのドラッグ速度
    float              rangeMin   = 0.0f;    // 範囲（min == max なら制限なし）
    float              rangeMax   = 0.0f;
    const char        *category   = nullptr; // インスペクタの折りたたみ見出し
    const char *const *enumNames  = nullptr; // enum 系でのみ使う
    int                enumCount  = 0;       // enum 系でのみ使う
    bool               serialize  = true;    // false なら保存しない（読込もしない）
    const char        *tooltip    = nullptr; // ホバー時の説明（nullptr なら無し）
    bool               readonly   = false;   // true なら表示だけで編集できない
    const char        *filePreset = nullptr; // null 以外なら String をファイルダイアログ付きで描く（FileDialog のプリセット名。例 "Texture"）
};

// FieldDescriptor を作る関数。引数の順は全種類で共通:
//   表示名, JSON キー, offset, （種別ごとの追加引数...）, category, serialize, tooltip, readonly
inline FieldDescriptor FD_Float(const char *name, const char *jsonName, size_t offset,
                                float speed = 0.05f, float rmin = 0.0f, float rmax = 0.0f,
                                const char *category = nullptr, bool serialize = true,
                                const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::Float, offset, speed, rmin, rmax, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_Int(const char *name, const char *jsonName, size_t offset,
                              float rmin = 0.0f, float rmax = 0.0f,
                              const char *category = nullptr, bool serialize = true,
                              const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::Int, offset, 0.05f, rmin, rmax, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_Bool(const char *name, const char *jsonName, size_t offset,
                               const char *category = nullptr, bool serialize = true,
                               const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::Bool, offset, 0.05f, 0.0f, 0.0f, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_UInt8(const char *name, const char *jsonName, size_t offset,
                                float speed = 1.0f, float rmin = 0.0f, float rmax = 0.0f,
                                const char *category = nullptr, bool serialize = true,
                                const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::UInt8, offset, speed, rmin, rmax, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_UInt16(const char *name, const char *jsonName, size_t offset,
                                 float speed = 1.0f, float rmin = 0.0f, float rmax = 0.0f,
                                 const char *category = nullptr, bool serialize = true,
                                 const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::UInt16, offset, speed, rmin, rmax, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_UInt32(const char *name, const char *jsonName, size_t offset,
                                 float speed = 1.0f, float rmin = 0.0f, float rmax = 0.0f,
                                 const char *category = nullptr, bool serialize = true,
                                 const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::UInt32, offset, speed, rmin, rmax, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_EntityLink(const char *name, const char *jsonName, size_t offset,
                                     const char *category = nullptr, bool serialize = true,
                                     const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::EntityLink, offset, 0.0f, 0.0f, 0.0f, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_EnumU8(const char *name, const char *jsonName, size_t offset,
                                 const char *const *enumNames, int enumCount,
                                 const char *category = nullptr, bool serialize = true,
                                 const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::EnumU8, offset, 0.0f, 0.0f, 0.0f, category, enumNames, enumCount, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_EnumInt(const char *name, const char *jsonName, size_t offset,
                                  const char *const *enumNames, int enumCount,
                                  const char *category = nullptr, bool serialize = true,
                                  const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::EnumInt, offset, 0.0f, 0.0f, 0.0f, category, enumNames, enumCount, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_Float2(const char *name, const char *jsonName, size_t offset,
                                 float speed = 0.05f, const char *category = nullptr, bool serialize = true,
                                 const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::Float2, offset, speed, 0.0f, 0.0f, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_Float3(const char *name, const char *jsonName, size_t offset,
                                 float speed = 0.05f, const char *category = nullptr, bool serialize = true,
                                 const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::Float3, offset, speed, 0.0f, 0.0f, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_Float4(const char *name, const char *jsonName, size_t offset,
                                 float speed = 0.05f, const char *category = nullptr, bool serialize = true,
                                 const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::Float4, offset, speed, 0.0f, 0.0f, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_Color(const char *name, const char *jsonName, size_t offset,
                                const char *category = nullptr, bool serialize = true,
                                const char *tooltip = nullptr, bool readonly = false)
{
    return {name, jsonName, FieldKind::Color, offset, 0.0f, 0.0f, 0.0f, category, nullptr, 0, serialize, tooltip, readonly};
}
inline FieldDescriptor FD_String(const char *name, const char *jsonName, size_t offset,
                                 const char *category = nullptr, bool serialize = true,
                                 const char *tooltip = nullptr, bool readonly = false, const char *filePreset = nullptr)
{
    return {name, jsonName, FieldKind::String, offset, 0.0f, 0.0f, 0.0f, category, nullptr, 0, serialize, tooltip, readonly, filePreset};
}

// ===========================================================================
// ComponentMeta<T>: コンポーネント型ごとのメタデータ（この既定は未登録の型用）
//   displayName    : インスペクタ上の名前
//   hasCustomGui   : true なら CustomGui() で描く（ComponentMetaImGui）
//   isSerializable : false ならシーン・プレハブに保存しない（ComponentRegistry）
//   CustomGui()    : hasCustomGui == true のときの描画関数
//   Fields()       : 保存と自動インスペクタの対象フィールド
//   保存形式を変えたい場合はここではなく ComponentMetaJson::ComponentSerializer<T> を特殊化する。
// ===========================================================================
template <typename T>
struct ComponentMeta
{
    static constexpr const char *displayName       = "(unregistered)";
    static constexpr const char *category          = "Misc";
    static constexpr int         sortPriority      = 1000;  // 小さいほどインスペクタの上に並ぶ
    static constexpr bool        hiddenInInspector = false; // true ならインスペクタに出さない
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    // 値を変更したら true を返す（Undo の記録に使う）
    static bool CustomGui(T & /*comp*/, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        return false;
    }

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

// ===========================================================================
// SFINAE: ComponentMeta<T> が category / sortPriority / hiddenInInspector を持つか。
//   手書きの特殊化では書き忘れることがあるので、無ければ既定値を使うために調べる。
// ===========================================================================
template <typename T, typename = void>
struct HasCategory : std::false_type
{
};
template <typename T>
struct HasCategory<T, std::void_t<decltype(ComponentMeta<T>::category)>> : std::true_type
{
};

template <typename T, typename = void>
struct HasSortPriority : std::false_type
{
};
template <typename T>
struct HasSortPriority<T, std::void_t<decltype(ComponentMeta<T>::sortPriority)>> : std::true_type
{
};

template <typename T, typename = void>
struct HasHiddenInInspector : std::false_type
{
};
template <typename T>
struct HasHiddenInInspector<T, std::void_t<decltype(ComponentMeta<T>::hiddenInInspector)>> : std::true_type
{
};

// 「先頭アドレス + offset」の位置を FieldT として参照する（FieldDescriptor.offset でメンバーを読むため）
template <typename FieldT>
inline FieldT &RawField(void *base, size_t offset)
{
    return *reinterpret_cast<FieldT *>(static_cast<char *>(base) + offset);
}
template <typename FieldT>
inline const FieldT &RawField(const void *base, size_t offset)
{
    return *reinterpret_cast<const FieldT *>(static_cast<const char *>(base) + offset);
}