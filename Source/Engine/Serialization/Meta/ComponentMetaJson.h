// =====================================================================
// ComponentMetaJson.h
//   FieldDescriptor を見て JSON ⇔ コンポーネントを変換する。
//   ★ 旧版との最大の違い:
//     - SerializeField / DeserializeField が SaveContext/LoadContext を受け取る
//     - FieldKind::EntityLink を見つけたら自動でローカルインデックス変換
//     - SceneSerializer.cpp 側のハードコード分岐 (if "Transform" / if "CombatRoster")
//       は不要になる。これがこの設計のキモ。
//
//   設計判断:
//     - HasCustomSerialize の Trait は維持 (特殊な型は手書きシリアライズに逃がせる)
//     - Context が nullptr の場合は EntityLink を生 ID として扱う (互換動作)
// =====================================================================
#pragma once
#include "ComponentMeta.h"
#include "Engine/Serialization/EntityLink.h"
#include "Engine/Serialization/SaveContext.h"
#include "Engine/Serialization/LoadContext.h"
#include <DirectXMath.h>
#include <json.hpp>
#include <type_traits>


namespace _MetaJsonDX {
    inline void F2_to(nlohmann::json& j, const DirectX::XMFLOAT2& v)
    {
        j = nlohmann::json::array({ v.x, v.y });
    }
    inline void F2_from(const nlohmann::json& j, DirectX::XMFLOAT2& v)
    {
        if (j.is_array() && j.size() >= 2) {
            v.x = j[0];
            v.y = j[1];
        }
    }
    inline void F3_to(nlohmann::json& j, const DirectX::XMFLOAT3& v)
    {
        j = nlohmann::json::array({ v.x, v.y, v.z });
    }
    inline void F3_from(const nlohmann::json& j, DirectX::XMFLOAT3& v)
    {
        if (j.is_array() && j.size() >= 3) {
            v.x = j[0];
            v.y = j[1];
            v.z = j[2];
        }
    }
    inline void F4_to(nlohmann::json& j, const DirectX::XMFLOAT4& v)
    {
        j = nlohmann::json::array({ v.x, v.y, v.z, v.w });
    }
    inline void F4_from(const nlohmann::json& j, DirectX::XMFLOAT4& v)
    {
        if (j.is_array() && j.size() >= 4) {
            v.x = j[0];
            v.y = j[1];
            v.z = j[2];
            v.w = j[3];
        }
    }
} // namespace _MetaJsonDX

namespace ComponentMetaJson {
    using json = nlohmann::json;
    using CCL::Serialization::SaveContext;
    using CCL::Serialization::LoadContext;
    using CCL::Serialization::EntityLink;

    // ------------------------------------------------------------------------
    // 特殊な型は手書きで Serialize したい (例: 動的配列を含むコンポーネント)
    // 専用化する側で true_type に派生させる。
    // ------------------------------------------------------------------------
    template <typename T>
    struct HasCustomSerialize : std::false_type {};

    // ------------------------------------------------------------------------
    // SerializeField (Save)
    //   - saveCtx が渡されていれば EntityLink を localIndex に変換
    //   - 渡されていない場合 (nullptr) は生 ID を書く (旧互換)
    // ------------------------------------------------------------------------
    inline void SerializeField(json& j, const FieldDescriptor& fd, const void* obj, SaveContext* saveCtx)
    {
        if (!fd.serialize || !fd.jsonName) return;

        switch (fd.kind) {
        case FieldKind::Float:    j[fd.jsonName] = RawField<const float>(obj, fd.offset); break;
        case FieldKind::Int:      j[fd.jsonName] = RawField<const int>(obj, fd.offset); break;
        case FieldKind::Bool:     j[fd.jsonName] = RawField<const bool>(obj, fd.offset); break;
        case FieldKind::UInt8:    j[fd.jsonName] = RawField<const uint8_t>(obj, fd.offset); break;
        case FieldKind::UInt16:   j[fd.jsonName] = RawField<const uint16_t>(obj, fd.offset); break;
        case FieldKind::UInt32:   j[fd.jsonName] = RawField<const uint32_t>(obj, fd.offset); break;

        case FieldKind::EntityID: // [旧形式] 生 ID をそのまま書く
            j[fd.jsonName] = RawField<const uint64_t>(obj, fd.offset);
            break;

        case FieldKind::EntityLink: { // ★ 自動でローカルインデックスに変換
            const EntityLink& link = RawField<const EntityLink>(obj, fd.offset);
            if (saveCtx) {
                // 正常パス: 0,1,2,... の連番を書き出す
                j[fd.jsonName] = saveCtx->GetOrRegister(link.id);
            }
            else {
                // フォールバック: 生 ID を書く (デバッグ用途・エディタ表示等)
                j[fd.jsonName] = static_cast<uint64_t>(link.id);
            }
            break;
        }

        case FieldKind::EnumU8:   j[fd.jsonName] = RawField<const uint8_t>(obj, fd.offset); break;
        case FieldKind::EnumInt:  j[fd.jsonName] = RawField<const int>(obj, fd.offset); break;
        case FieldKind::Float2:   _MetaJsonDX::F2_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT2>(obj, fd.offset)); break;
        case FieldKind::Float3:   _MetaJsonDX::F3_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT3>(obj, fd.offset)); break;
        case FieldKind::Float4:   _MetaJsonDX::F4_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT4>(obj, fd.offset)); break;
        case FieldKind::String:   j[fd.jsonName] = RawField<const std::string>(obj, fd.offset); break;
        }
    }

    // ------------------------------------------------------------------------
    // DeserializeField (Load)
    //   - loadCtx が渡されていれば EntityLink を localIndex から解決
    //   - 渡されていない場合 (nullptr) は生 ID を読む (旧互換)
    //
    //   ★ Pass 順序の前提:
    //     Pass 1 でエンティティが全部生成され、LoadContext::Register 済み
    //     Pass 2 でこの関数が呼ばれるので、Resolve が必ず成功する。
    // ------------------------------------------------------------------------
    inline void DeserializeField(const json& j, const FieldDescriptor& fd, void* obj, LoadContext* loadCtx)
    {
        if (!fd.serialize || !fd.jsonName || !j.contains(fd.jsonName)) return;

        switch (fd.kind) {
        case FieldKind::Float:    j[fd.jsonName].get_to(RawField<float>(obj, fd.offset)); break;
        case FieldKind::Int:      j[fd.jsonName].get_to(RawField<int>(obj, fd.offset)); break;
        case FieldKind::Bool:     j[fd.jsonName].get_to(RawField<bool>(obj, fd.offset)); break;
        case FieldKind::UInt8:    j[fd.jsonName].get_to(RawField<uint8_t>(obj, fd.offset)); break;
        case FieldKind::UInt16:   j[fd.jsonName].get_to(RawField<uint16_t>(obj, fd.offset)); break;
        case FieldKind::UInt32:   j[fd.jsonName].get_to(RawField<uint32_t>(obj, fd.offset)); break;

        case FieldKind::EntityID: // [旧形式] 生 ID をそのまま読む (※新しいシーンでは壊れる可能性)
            j[fd.jsonName].get_to(RawField<uint64_t>(obj, fd.offset));
            break;

        case FieldKind::EntityLink: { // ★ ローカルインデックスから EntityID へ自動解決
            EntityLink& link = RawField<EntityLink>(obj, fd.offset);

            // 数値型の混在ケースに備えてフォールバック付きで読む。
            // (旧フォーマットからのマイグレーション中は uint64 が混入する可能性がある)
            if (j[fd.jsonName].is_number_unsigned()) {
                uint64_t raw = j[fd.jsonName].get<uint64_t>();
                if (loadCtx) {
                    // ローカルインデックスとして解釈し、実 EntityID に解決
                    link.id = loadCtx->Resolve(static_cast<SaveContext::LocalIndex>(raw));
                }
                else {
                    // Context なし: 生 ID として受け取る (互換動作)
                    link.id = raw;
                }
            }
            else {
                link.Reset();
            }
            break;
        }

        case FieldKind::EnumU8:   j[fd.jsonName].get_to(RawField<uint8_t>(obj, fd.offset)); break;
        case FieldKind::EnumInt:  j[fd.jsonName].get_to(RawField<int>(obj, fd.offset)); break;
        case FieldKind::Float2:   _MetaJsonDX::F2_from(j[fd.jsonName], RawField<DirectX::XMFLOAT2>(obj, fd.offset)); break;
        case FieldKind::Float3:   _MetaJsonDX::F3_from(j[fd.jsonName], RawField<DirectX::XMFLOAT3>(obj, fd.offset)); break;
        case FieldKind::Float4:   _MetaJsonDX::F4_from(j[fd.jsonName], RawField<DirectX::XMFLOAT4>(obj, fd.offset)); break;
        case FieldKind::String:   j[fd.jsonName].get_to(RawField<std::string>(obj, fd.offset)); break;
        }
    }


    // ------------------------------------------------------------------------
    // 公開 API
    //   Context を取る版 / 取らない版の両方を用意 (既存コードの互換のため)
    // ------------------------------------------------------------------------
    template <typename T> void Serialize(json& j, const T& component, SaveContext* ctx = nullptr)
    {
        if constexpr (HasCustomSerialize<T>::value) {
            ComponentMeta<T>::serialize(j, &component);
        }
        else {
            const void* obj = &component;
            for (const auto& fd : ComponentMeta<T>::Fields()) {
                SerializeField(j, fd, obj, ctx);
            }
        }
    }

    template <typename T> void Deserialize(const json& j, T& component, LoadContext* ctx = nullptr)
    {
        void* obj = &component;
        for (const auto& fd : ComponentMeta<T>::Fields()) {
            DeserializeField(j, fd, obj, ctx);
        }
    }

    // ------------------------------------------------------------------------
    // Registry への登録ヘルパー
    //   ComponentRegistry::MetaInfo に格納されるラムダ群はこの関数経由で生成する。
    //   void* で型を消した状態でも、ここで保存したテンプレート情報により
    //   FieldDescriptor のループで正しく処理できる。
    // ------------------------------------------------------------------------
    template <typename T, typename RegistryType>
    void RegisterMetaT(RegistryType& registry, const std::string& name)
    {
        typename RegistryType::MetaInfo info;
        info.name = name;

        info.serialize = [](json& j, const void* ptr, SaveContext* ctx) {
            Serialize(j, *static_cast<const T*>(ptr), ctx);
            };

        info.deserializeToWorld = [](auto* world, auto entity, const json& j, LoadContext* ctx) {
            if constexpr (HasCustomSerialize<T>::value) {
                ComponentMeta<T>::deserializeToWorld(world, entity, j, ctx);
            }
            else {
                T temp{};
                Deserialize(j, temp, ctx);
                world->template AddComponent<T>(entity, std::move(temp));
            }
            };

        registry.RegisterInternal(CCL::ECS::TypeInfo<T>::ID(), name, std::move(info));
    }

} // namespace ComponentMetaJson