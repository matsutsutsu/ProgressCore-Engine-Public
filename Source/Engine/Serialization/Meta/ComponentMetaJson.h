// =====================================================================
// ComponentMetaJson.h
//   コンポーネント ⇔ JSON 変換。
//
//   リファクタリング方針:
//     - シリアライズ経路を「1 つ」に統一した。
//       唯一のカスタマイズ口は ComponentSerializer<T> 構造体テンプレート。
//         既定動作: ComponentMeta<T>::Fields() を走査して自動変換。
//         カスタム : ComponentSerializer<T> を特殊化し Save/Load を書く。
//     - 旧 HasCustomSerialize トレイト / hasCustomSerialize メンバー /
//       関数テンプレート Serialize<T> の明示的特殊化 / 二重 if constexpr 分岐は
//       すべて撤去。経路が分岐しない。
//     - FieldKind::EntityID は撤去 (EntityLink のみ)。
// =====================================================================
#pragma once
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

    // -----------------------------------------------------------------------
    // フィールド単位の変換 (FieldDescriptor 1 つ分)
    // -----------------------------------------------------------------------
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
            // エンティティ参照は raw ID ではなくローカルインデックスで書き出す。
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

    // =====================================================================
    // 唯一のシリアライズ・カスタマイズ口
    //
    //   既定 (特殊化なし): ComponentMeta<T>::Fields() を走査して全自動変換。
    //   カスタム          : この構造体を特殊化し、Save / Load を自前で書く。
    //
    //   使い方 (カスタム例):
    //     namespace ComponentMetaJson {
    //         template <>
    //         struct ComponentSerializer<MyComponent> {
    //             static void Save(json& j, const MyComponent& c, SaveContext* ctx) { ... }
    //             static void Load(const json& j, MyComponent& c, LoadContext* ctx) { ... }
    //         };
    //     }
    //
    //   経路はこれ 1 本だけ。フラグもトレイトも分岐も無い。
    // =====================================================================
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

    // -----------------------------------------------------------------------
    // 利便関数 (常に ComponentSerializer<T> を経由する薄いラッパ)。
    //   呼び出し側はこちらを使ってよい。挙動は完全に ComponentSerializer に従う。
    // -----------------------------------------------------------------------
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