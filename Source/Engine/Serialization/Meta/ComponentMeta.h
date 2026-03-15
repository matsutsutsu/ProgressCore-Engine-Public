#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

enum class FieldKind : uint8_t {
    Float,
    Int,
    Bool,
    UInt8,
    EnumU8,
    EnumInt,
    Float2,
    Float3,
    Float4,
    String
};

struct FieldDescriptor {
    const char        *name      = nullptr;
    FieldKind          kind      = FieldKind::Float;
    size_t             offset    = 0;
    float              dragSpeed = 0.05f;
    float              rangeMin  = 0.0f;
    float              rangeMax  = 0.0f;
    const char        *category  = nullptr;
    const char *const *enumNames = nullptr;
    int                enumCount = 0;
    bool               serialize = true;
};

inline FieldDescriptor FD_Float(const char *name,
    size_t                                  offset,
    float                                   speed    = 0.05f,
    float                                   rangeMin = 0.0f,
    float                                   rangeMax = 0.0f,
    const char                             *category = nullptr)
{
    FieldDescriptor d{};
    d.name      = name;
    d.kind      = FieldKind::Float;
    d.offset    = offset;
    d.dragSpeed = speed;
    d.rangeMin  = rangeMin;
    d.rangeMax  = rangeMax;
    d.category  = category;
    return d;
}
inline FieldDescriptor FD_Int(const char *name,
    size_t                                offset,
    float                                 rangeMin = 0.0f,
    float                                 rangeMax = 0.0f,
    const char                           *category = nullptr)
{
    FieldDescriptor d{};
    d.name      = name;
    d.kind      = FieldKind::Int;
    d.offset    = offset;
    d.dragSpeed = 1.0f;
    d.rangeMin  = rangeMin;
    d.rangeMax  = rangeMax;
    d.category  = category;
    return d;
}
inline FieldDescriptor FD_Bool(const char *name, size_t offset, const char *category = nullptr)
{
    FieldDescriptor d{};
    d.name     = name;
    d.kind     = FieldKind::Bool;
    d.offset   = offset;
    d.category = category;
    return d;
}
inline FieldDescriptor FD_EnumU8(const char *name,
    size_t                                   offset,
    const char *const                       *enumNames,
    int                                      enumCount,
    const char                              *category = nullptr)
{
    FieldDescriptor d{};
    d.name      = name;
    d.kind      = FieldKind::EnumU8;
    d.offset    = offset;
    d.enumNames = enumNames;
    d.enumCount = enumCount;
    d.category  = category;
    return d;
}
inline FieldDescriptor FD_EnumInt(const char *name,
    size_t                                    offset,
    const char *const                        *enumNames,
    int                                       enumCount,
    const char                               *category = nullptr)
{
    FieldDescriptor d{};
    d.name      = name;
    d.kind      = FieldKind::EnumInt;
    d.offset    = offset;
    d.enumNames = enumNames;
    d.enumCount = enumCount;
    d.category  = category;
    return d;
}
inline FieldDescriptor FD_Float3(
    const char *name, size_t offset, float speed = 0.05f, const char *category = nullptr)
{
    FieldDescriptor d{};
    d.name      = name;
    d.kind      = FieldKind::Float3;
    d.offset    = offset;
    d.dragSpeed = speed;
    d.category  = category;
    return d;
}
inline FieldDescriptor FD_RuntimeOnly(FieldDescriptor d)
{
    d.serialize = false;
    return d;
}

template <typename T> struct ComponentMeta {
    static constexpr bool                      registered  = false;
    static constexpr const char               *displayName = "(unregistered)";
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};


// コンポーネントの先頭アドレス（void* base）と、ズレ（size_t offset）を受け取る
template <typename FieldT> inline FieldT &RawField(void *base, size_t offset)
{
    // ① static_cast<char *>(base)
    // 「void*」は単なる「場所」なので、何歩進めばいいかわかりません。
    // 「char*」に変換することで、「1歩 ＝ 1バイトずつ進める状態」にします。

    // ② + offset
    // 先頭アドレスから、指定されたバイト数（歩数）だけ進みます。
    // 例：attackRangeなら「4」なので、1000番地 + 4 = 1004番地 に到着します。

    // ③ reinterpret_cast<FieldT *>( ... )
    // 1004番地に到着しましたが、コンピュータはそこに何が入っているか知りません。
    // なので、「ここにある4バイトは float（FieldT）として読み取ってね！」と強制的に教えます。
    return *reinterpret_cast<FieldT *>(static_cast<char *>(base) + offset);
}
template <typename FieldT> inline const FieldT &RawField(const void *base, size_t offset)
{
    return *reinterpret_cast<const FieldT *>(static_cast<const char *>(base) + offset);
}