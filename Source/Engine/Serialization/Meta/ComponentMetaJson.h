#pragma once
// ===========================================================================
// 概要: コンポーネントと JSON の相互変換
//
// 責務:
//   - FieldDescriptor 1 つ分の JSON への書き出しと読込（SerializeField / DeserializeField）
//   - コンポーネント全体の変換（ComponentSerializer<T>）
//
// 備考:
//   - 保存形式を変える方法は ComponentSerializer<T> の特殊化だけ。既定は Fields() を順に変換する
//   - EntityLink は SaveContext / LoadContext でローカルインデックスと EntityID を変換する
// ===========================================================================
#include "ComponentMeta.h"
#include "Engine/Serialization/Core/EntityLink.h"
#include "Engine/Serialization/Core/SaveContext.h"
#include "Engine/Serialization/Core/LoadContext.h"
#include <DirectXMath.h>
#include <json.hpp>
#include <cassert>

namespace _MetaJsonDX
{
    inline void F2_to(nlohmann::json &j, const DirectX::XMFLOAT2 &v) { j = nlohmann::json::array({v.x, v.y}); }
    inline void F2_from(const nlohmann::json &j, DirectX::XMFLOAT2 &v)
    {
        if (j.is_array() && j.size() >= 2)
        {
            v.x = j[0];
            v.y = j[1];
        }
    }
    inline void F3_to(nlohmann::json &j, const DirectX::XMFLOAT3 &v) { j = nlohmann::json::array({v.x, v.y, v.z}); }
    inline void F3_from(const nlohmann::json &j, DirectX::XMFLOAT3 &v)
    {
        if (j.is_array() && j.size() >= 3)
        {
            v.x = j[0];
            v.y = j[1];
            v.z = j[2];
        }
    }
    inline void F4_to(nlohmann::json &j, const DirectX::XMFLOAT4 &v) { j = nlohmann::json::array({v.x, v.y, v.z, v.w}); }
    inline void F4_from(const nlohmann::json &j, DirectX::XMFLOAT4 &v)
    {
        if (j.is_array() && j.size() >= 4)
        {
            v.x = j[0];
            v.y = j[1];
            v.z = j[2];
            v.w = j[3];
        }
    }
} // namespace _MetaJsonDX

namespace ComponentMetaJson
{
    using json = nlohmann::json;
    using CCL::Serialization::EntityLink;
    using CCL::Serialization::LoadContext;
    using CCL::Serialization::SaveContext;

    // FieldDescriptor 1 つ分を JSON に書く
    inline void SerializeField(json &j, const FieldDescriptor &fd, const void *obj, SaveContext *saveCtx)
    {
        if (!fd.serialize || !fd.jsonName) return;

        switch (fd.kind)
        {
        case FieldKind::Float:
            j[fd.jsonName] = RawField<const float>(obj, fd.offset);
            break;
        case FieldKind::Int:
            j[fd.jsonName] = RawField<const int>(obj, fd.offset);
            break;
        case FieldKind::Bool:
            j[fd.jsonName] = RawField<const bool>(obj, fd.offset);
            break;
        case FieldKind::UInt8:
            j[fd.jsonName] = RawField<const uint8_t>(obj, fd.offset);
            break;
        case FieldKind::UInt16:
            j[fd.jsonName] = RawField<const uint16_t>(obj, fd.offset);
            break;
        case FieldKind::UInt32:
            j[fd.jsonName] = RawField<const uint32_t>(obj, fd.offset);
            break;

        case FieldKind::EntityLink: {
            // Entity への参照は EntityID ではなくローカルインデックスで書く（ctx がなければ EntityID のまま）
            const EntityLink &link = RawField<const EntityLink>(obj, fd.offset);
            j[fd.jsonName]         = saveCtx ? saveCtx->GetOrRegister(link.id)
                                             : static_cast<uint64_t>(link.id);
            break;
        }

        case FieldKind::EnumU8:
            j[fd.jsonName] = RawField<const uint8_t>(obj, fd.offset);
            break;
        case FieldKind::EnumInt:
            j[fd.jsonName] = RawField<const int>(obj, fd.offset);
            break;
        case FieldKind::Float2:
            _MetaJsonDX::F2_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT2>(obj, fd.offset));
            break;
        case FieldKind::Float3:
            _MetaJsonDX::F3_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT3>(obj, fd.offset));
            break;
        case FieldKind::Float4:
            _MetaJsonDX::F4_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT4>(obj, fd.offset));
            break;
        case FieldKind::Color:
            _MetaJsonDX::F4_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT4>(obj, fd.offset));
            break;
        case FieldKind::String:
            j[fd.jsonName] = RawField<const std::string>(obj, fd.offset);
            break;
        default:
            assert(false && "SerializeField: unhandled FieldKind");
            break;
        }
    }

    // FieldDescriptor 1 つ分を JSON から読む。キーがなければ既定値のまま
    inline void DeserializeField(const json &j, const FieldDescriptor &fd, void *obj, LoadContext *loadCtx)
    {
        if (!fd.serialize || !fd.jsonName || !j.contains(fd.jsonName)) return;

        switch (fd.kind)
        {
        case FieldKind::Float:
            j[fd.jsonName].get_to(RawField<float>(obj, fd.offset));
            break;
        case FieldKind::Int:
            j[fd.jsonName].get_to(RawField<int>(obj, fd.offset));
            break;
        case FieldKind::Bool:
            j[fd.jsonName].get_to(RawField<bool>(obj, fd.offset));
            break;
        case FieldKind::UInt8:
            j[fd.jsonName].get_to(RawField<uint8_t>(obj, fd.offset));
            break;
        case FieldKind::UInt16:
            j[fd.jsonName].get_to(RawField<uint16_t>(obj, fd.offset));
            break;
        case FieldKind::UInt32:
            j[fd.jsonName].get_to(RawField<uint32_t>(obj, fd.offset));
            break;

        case FieldKind::EntityLink: {
            EntityLink &link = RawField<EntityLink>(obj, fd.offset);
            if (j[fd.jsonName].is_number_unsigned())
            {
                uint64_t raw = j[fd.jsonName].get<uint64_t>();
                link.id      = loadCtx ? loadCtx->Resolve(static_cast<SaveContext::LocalIndex>(raw))
                                       : raw;
            }
            else
            {
                link.Reset();
            }
            break;
        }

        case FieldKind::EnumU8:
            j[fd.jsonName].get_to(RawField<uint8_t>(obj, fd.offset));
            break;
        case FieldKind::EnumInt:
            j[fd.jsonName].get_to(RawField<int>(obj, fd.offset));
            break;
        case FieldKind::Float2:
            _MetaJsonDX::F2_from(j[fd.jsonName], RawField<DirectX::XMFLOAT2>(obj, fd.offset));
            break;
        case FieldKind::Float3:
            _MetaJsonDX::F3_from(j[fd.jsonName], RawField<DirectX::XMFLOAT3>(obj, fd.offset));
            break;
        case FieldKind::Float4:
            _MetaJsonDX::F4_from(j[fd.jsonName], RawField<DirectX::XMFLOAT4>(obj, fd.offset));
            break;
        case FieldKind::Color:
            _MetaJsonDX::F4_from(j[fd.jsonName], RawField<DirectX::XMFLOAT4>(obj, fd.offset));
            break;
        case FieldKind::String:
            j[fd.jsonName].get_to(RawField<std::string>(obj, fd.offset));
            break;
        default:
            assert(false && "DeserializeField: unhandled FieldKind");
            break;
        }
    }

    // コンポーネント全体の変換。既定は Fields() を順に変換する
    // 保存の形を変えたいときは、この構造体を特殊化して Save / Load を書く
    template <typename T>
    struct ComponentSerializer
    {
        static void Save(json &j, const T &component, SaveContext *ctx)
        {
            const void *obj = &component;
            for (const auto &fd : ComponentMeta<T>::Fields())
                SerializeField(j, fd, obj, ctx);
        }
        static void Load(const json &j, T &component, LoadContext *ctx)
        {
            void *obj = &component;
            for (const auto &fd : ComponentMeta<T>::Fields())
                DeserializeField(j, fd, obj, ctx);
        }
    };

    // ComponentSerializer<T> を呼ぶだけの関数（型を推論させて呼べる）
    template <typename T>
    inline void Serialize(json &j, const T &component, SaveContext *ctx = nullptr)
    {
        ComponentSerializer<T>::Save(j, component, ctx);
    }
    template <typename T>
    inline void Deserialize(const json &j, T &component, LoadContext *ctx = nullptr)
    {
        ComponentSerializer<T>::Load(j, component, ctx);
    }

} // namespace ComponentMetaJson