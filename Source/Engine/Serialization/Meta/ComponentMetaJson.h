// =====================================================================
// ComponentMetaJson.h
//   FieldDescriptor を見て JSON ⇔ コンポーネントを変換する。
//
//   修正履歴 v3:
//     - RegisterMetaT を削除 (ComponentRegistry::Register<T> と重複、競合の原因)
//     - Serialize / Deserialize の public 関数シグネチャを統一
//       (旧) Serialize(json&, const T&)
//       (新) Serialize(json&, const T&, SaveContext* = nullptr)
//     - 明示的特化 (ModelComponent, NameComponent 等) は .cpp 側で
//       (新シグネチャ) Serialize<T>(json&, const T&, SaveContext*) に合わせること
// =====================================================================
#pragma once
#include "ComponentMeta.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Engine/Serialization/Core/SaveContext.h"
#include "Engine/Serialization/Core/LoadContext.h"
#include <DirectXMath.h>
#include <json.hpp>
#include <type_traits>

namespace _MetaJsonDX {
    inline void F2_to(nlohmann::json& j, const DirectX::XMFLOAT2& v) { j = nlohmann::json::array({ v.x, v.y }); }
    inline void F2_from(const nlohmann::json& j, DirectX::XMFLOAT2& v) {
        if (j.is_array() && j.size() >= 2) { v.x = j[0]; v.y = j[1]; }
    }
    inline void F3_to(nlohmann::json& j, const DirectX::XMFLOAT3& v) { j = nlohmann::json::array({ v.x, v.y, v.z }); }
    inline void F3_from(const nlohmann::json& j, DirectX::XMFLOAT3& v) {
        if (j.is_array() && j.size() >= 3) { v.x = j[0]; v.y = j[1]; v.z = j[2]; }
    }
    inline void F4_to(nlohmann::json& j, const DirectX::XMFLOAT4& v) { j = nlohmann::json::array({ v.x, v.y, v.z, v.w }); }
    inline void F4_from(const nlohmann::json& j, DirectX::XMFLOAT4& v) {
        if (j.is_array() && j.size() >= 4) { v.x = j[0]; v.y = j[1]; v.z = j[2]; v.w = j[3]; }
    }
} // namespace _MetaJsonDX

namespace ComponentMetaJson {
    using json = nlohmann::json;
    using CCL::Serialization::SaveContext;
    using CCL::Serialization::LoadContext;
    using CCL::Serialization::EntityLink;

    // -----------------------------------------------------------------------
    // HasCustomSerialize
    //   true_type に特化すると、Serialize/Deserialize の Fields() ループを
    //   バイパスして ComponentMeta<T>::serialize() / deserializeToWorld() を呼ぶ。
    //   ModelComponent / NameComponent のように explicit specialization で
    //   直接 Serialize<T> を上書きする場合はこのフラグ不要。
    // -----------------------------------------------------------------------
    template <typename T>
    struct HasCustomSerialize : std::false_type {};

    // -----------------------------------------------------------------------
    // SerializeField / DeserializeField
    // -----------------------------------------------------------------------
    inline void SerializeField(json& j, const FieldDescriptor& fd, const void* obj, SaveContext* saveCtx)
    {
        if (!fd.serialize || !fd.jsonName) return;

        switch (fd.kind) {
        case FieldKind::Float:      j[fd.jsonName] = RawField<const float>(obj, fd.offset); break;
        case FieldKind::Int:        j[fd.jsonName] = RawField<const int>(obj, fd.offset); break;
        case FieldKind::Bool:       j[fd.jsonName] = RawField<const bool>(obj, fd.offset); break;
        case FieldKind::UInt8:      j[fd.jsonName] = RawField<const uint8_t>(obj, fd.offset); break;
        case FieldKind::UInt16:     j[fd.jsonName] = RawField<const uint16_t>(obj, fd.offset); break;
        case FieldKind::UInt32:     j[fd.jsonName] = RawField<const uint32_t>(obj, fd.offset); break;

        case FieldKind::EntityID:   // [旧形式] 生 ID をそのまま書く
            j[fd.jsonName] = RawField<const uint64_t>(obj, fd.offset);
            break;

        case FieldKind::EntityLink: { // ★ 自動でローカルインデックスに変換
            const EntityLink& link = RawField<const EntityLink>(obj, fd.offset);
            j[fd.jsonName] = saveCtx ? saveCtx->GetOrRegister(link.id)
                : static_cast<uint64_t>(link.id);
            break;
        }

        case FieldKind::EnumU8:     j[fd.jsonName] = RawField<const uint8_t>(obj, fd.offset); break;
        case FieldKind::EnumInt:    j[fd.jsonName] = RawField<const int>(obj, fd.offset); break;
        case FieldKind::Float2:     _MetaJsonDX::F2_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT2>(obj, fd.offset)); break;
        case FieldKind::Float3:     _MetaJsonDX::F3_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT3>(obj, fd.offset)); break;
        case FieldKind::Float4:     _MetaJsonDX::F4_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT4>(obj, fd.offset)); break;
        case FieldKind::String:     j[fd.jsonName] = RawField<const std::string>(obj, fd.offset); break;
        }
    }

    inline void DeserializeField(const json& j, const FieldDescriptor& fd, void* obj, LoadContext* loadCtx)
    {
        if (!fd.serialize || !fd.jsonName || !j.contains(fd.jsonName)) return;

        switch (fd.kind) {
        case FieldKind::Float:      j[fd.jsonName].get_to(RawField<float>(obj, fd.offset)); break;
        case FieldKind::Int:        j[fd.jsonName].get_to(RawField<int>(obj, fd.offset)); break;
        case FieldKind::Bool:       j[fd.jsonName].get_to(RawField<bool>(obj, fd.offset)); break;
        case FieldKind::UInt8:      j[fd.jsonName].get_to(RawField<uint8_t>(obj, fd.offset)); break;
        case FieldKind::UInt16:     j[fd.jsonName].get_to(RawField<uint16_t>(obj, fd.offset)); break;
        case FieldKind::UInt32:     j[fd.jsonName].get_to(RawField<uint32_t>(obj, fd.offset)); break;
        case FieldKind::EntityID:   j[fd.jsonName].get_to(RawField<uint64_t>(obj, fd.offset)); break;

        case FieldKind::EntityLink: {
            EntityLink& link = RawField<EntityLink>(obj, fd.offset);
            if (j[fd.jsonName].is_number_unsigned()) {
                uint64_t raw = j[fd.jsonName].get<uint64_t>();
                link.id = loadCtx
                    ? loadCtx->Resolve(static_cast<SaveContext::LocalIndex>(raw))
                    : raw;
            }
            else {
                link.Reset();
            }
            break;
        }

        case FieldKind::EnumU8:     j[fd.jsonName].get_to(RawField<uint8_t>(obj, fd.offset)); break;
        case FieldKind::EnumInt:    j[fd.jsonName].get_to(RawField<int>(obj, fd.offset)); break;
        case FieldKind::Float2:     _MetaJsonDX::F2_from(j[fd.jsonName], RawField<DirectX::XMFLOAT2>(obj, fd.offset)); break;
        case FieldKind::Float3:     _MetaJsonDX::F3_from(j[fd.jsonName], RawField<DirectX::XMFLOAT3>(obj, fd.offset)); break;
        case FieldKind::Float4:     _MetaJsonDX::F4_from(j[fd.jsonName], RawField<DirectX::XMFLOAT4>(obj, fd.offset)); break;
        case FieldKind::String:     j[fd.jsonName].get_to(RawField<std::string>(obj, fd.offset)); break;
        }
    }

    // -----------------------------------------------------------------------
    // Serialize / Deserialize  (公開 API)
    //
    //   ★ 重要: 明示的特化 (explicit specialization) をする .cpp 側は
    //           この完全なシグネチャで特化すること:
    //
    //   template <>
    //   void Serialize<MyComponent>(json& j, const MyComponent& comp, SaveContext* ctx) { ... }
    //
    //   template <>
    //   void Deserialize<MyComponent>(const json& j, MyComponent& comp, LoadContext* ctx) { ... }
    //
    //   旧シグネチャ (ctx なし) は「テンプレートの特化ではない」エラーになる。
    // -----------------------------------------------------------------------
    template <typename T>
    void Serialize(json& j, const T& component, SaveContext* ctx = nullptr)
    {
        if constexpr (HasCustomSerialize<T>::value) {
            // HasCustomSerialize<T> を true にした型は ComponentMeta<T>::serialize() を呼ぶ
            ComponentMeta<T>::serialize(j, &component);
        }
        else {
            const void* obj = &component;
            for (const auto& fd : ComponentMeta<T>::Fields()) {
                SerializeField(j, fd, obj, ctx);
            }
        }
    }

    template <typename T>
    void Deserialize(const json& j, T& component, LoadContext* ctx = nullptr)
    {
        void* obj = &component;
        for (const auto& fd : ComponentMeta<T>::Fields()) {
            DeserializeField(j, fd, obj, ctx);
        }
    }

    // -----------------------------------------------------------------------
    // RegisterMetaT は削除。
    //   ComponentRegistry::Register<T>() が同等の役割を担うため不要。
    //   両者が存在するとラムダのシグネチャ不整合や二重登録の原因になる。
    // -----------------------------------------------------------------------

} // namespace ComponentMetaJson