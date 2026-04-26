#pragma once
#include "ComponentMeta.h"
#include <DirectXMath.h>
#include <json.hpp>

namespace _MetaJsonDX {
    inline void F2_to(nlohmann::json &j, const DirectX::XMFLOAT2 &v)
    {
        j = nlohmann::json::array({v.x, v.y});
    }
    inline void F2_from(const nlohmann::json &j, DirectX::XMFLOAT2 &v)
    {
        if (j.is_array() && j.size() >= 2) {
            v.x = j[0];
            v.y = j[1];
        }
    }
    inline void F3_to(nlohmann::json &j, const DirectX::XMFLOAT3 &v)
    {
        j = nlohmann::json::array({v.x, v.y, v.z});
    }
    inline void F3_from(const nlohmann::json &j, DirectX::XMFLOAT3 &v)
    {
        if (j.is_array() && j.size() >= 3) {
            v.x = j[0];
            v.y = j[1];
            v.z = j[2];
        }
    }
    inline void F4_to(nlohmann::json &j, const DirectX::XMFLOAT4 &v)
    {
        j = nlohmann::json::array({v.x, v.y, v.z, v.w});
    }
    inline void F4_from(const nlohmann::json &j, DirectX::XMFLOAT4 &v)
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

    // ------------------------------------------------------------------------
    // jsonName を使って保存する
    // ------------------------------------------------------------------------
    inline void SerializeField(json &j, const FieldDescriptor &fd, const void *obj)
    {
        if (!fd.serialize || !fd.jsonName) return; // jsonName が無い場合は安全のためスキップ

        switch (fd.kind) {
        case FieldKind::Float:   j[fd.jsonName] = RawField<const float>(obj, fd.offset); break;
        case FieldKind::Int:     j[fd.jsonName] = RawField<const int>(obj, fd.offset); break;
        case FieldKind::Bool:    j[fd.jsonName] = RawField<const bool>(obj, fd.offset); break;
        case FieldKind::UInt8:   j[fd.jsonName] = RawField<const uint8_t>(obj, fd.offset); break;
		case FieldKind::UInt16:  j[fd.jsonName] = RawField<const uint16_t>(obj, fd.offset); break;
		case FieldKind::UInt32:  j[fd.jsonName] = RawField<const uint32_t>(obj, fd.offset); break;
		case FieldKind::EntityID: j[fd.jsonName] = RawField<const uint64_t>(obj, fd.offset); break; // EntityIDはuint64_tとして保存
        case FieldKind::EnumU8:  j[fd.jsonName] = RawField<const uint8_t>(obj, fd.offset); break;
        case FieldKind::EnumInt: j[fd.jsonName] = RawField<const int>(obj, fd.offset); break;
        case FieldKind::Float2:  _MetaJsonDX::F2_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT2>(obj, fd.offset)); break;
        case FieldKind::Float3:  _MetaJsonDX::F3_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT3>(obj, fd.offset)); break;
        case FieldKind::Float4:  _MetaJsonDX::F4_to(j[fd.jsonName], RawField<const DirectX::XMFLOAT4>(obj, fd.offset)); break;
        case FieldKind::String:  j[fd.jsonName] = RawField<const std::string>(obj, fd.offset); break;
        }
    }

    // ------------------------------------------------------------------------
    // jsonName を使って読み込む
    // ------------------------------------------------------------------------
    inline void DeserializeField(const json &j, const FieldDescriptor &fd, void *obj)
    {
        if (!fd.serialize || !fd.jsonName || !j.contains(fd.jsonName)) return;

        switch (fd.kind) {
        case FieldKind::Float:   j[fd.jsonName].get_to(RawField<float>(obj, fd.offset)); break;
        case FieldKind::Int:     j[fd.jsonName].get_to(RawField<int>(obj, fd.offset)); break;
        case FieldKind::Bool:    j[fd.jsonName].get_to(RawField<bool>(obj, fd.offset)); break;
        case FieldKind::UInt8:   j[fd.jsonName].get_to(RawField<uint8_t>(obj, fd.offset)); break;
		case FieldKind::UInt16:  j[fd.jsonName].get_to(RawField<uint16_t>(obj, fd.offset)); break;
		case FieldKind::UInt32:  j[fd.jsonName].get_to(RawField<uint32_t>(obj, fd.offset)); break;
        case FieldKind::EntityID: j[fd.jsonName].get_to(RawField<uint64_t>(obj, fd.offset)); break;
        case FieldKind::EnumU8:  j[fd.jsonName].get_to(RawField<uint8_t>(obj, fd.offset)); break;
        case FieldKind::EnumInt: j[fd.jsonName].get_to(RawField<int>(obj, fd.offset)); break;
        case FieldKind::Float2:  _MetaJsonDX::F2_from(j[fd.jsonName], RawField<DirectX::XMFLOAT2>(obj, fd.offset)); break;
        case FieldKind::Float3:  _MetaJsonDX::F3_from(j[fd.jsonName], RawField<DirectX::XMFLOAT3>(obj, fd.offset)); break;
        case FieldKind::Float4:  _MetaJsonDX::F4_from(j[fd.jsonName], RawField<DirectX::XMFLOAT4>(obj, fd.offset)); break;
        case FieldKind::String:  j[fd.jsonName].get_to(RawField<std::string>(obj, fd.offset)); break;
        }
    }

    template <typename T> void Serialize(json &j, const T &component)
    {
        const void *obj = &component;
        for (const auto &fd : ComponentMeta<T>::Fields()) SerializeField(j, fd, obj);
    }
    template <typename T> void Deserialize(const json &j, T &component)
    {
        void *obj = &component;
        for (const auto &fd : ComponentMeta<T>::Fields()) DeserializeField(j, fd, obj);
    }

    template <typename T, typename RegistryType>
    void RegisterMetaT(RegistryType &registry, const std::string &name)
    {
        typename RegistryType::MetaInfo info;
        info.name      = name;
        info.serialize = [](json &j, const void *ptr) {
            Serialize(j, *static_cast<const T *>(ptr));
        };
        info.deserializeToWorld = [](auto *world, auto entity, const json &j) {
            // 1. 縺ｾ縺壹，++縺ｮ繝ｫ繝ｼ繝ｫ・・nemyComponent.h 縺ｮ蛻晄悄蛟､・峨〒縲∵眠縺励＞縲檎ｩｺ縺ｮ邂ｱ・・emp・峨阪ｒ菴懊ｋ
            T temp{};
            // 縺薙・譎らせ縺ｧ縲》emp縺ｮ荳ｭ霄ｫ縺ｯ moveSpeed=0.0f, attackRange=2.0f(繝・ヵ繧ｩ繝ｫ繝亥､)
            // 縺ｫ縺ｪ縺｣縺ｦ縺・ｋ縲・

            // 2. 繝ｭ繝懊ャ繝医′JSON縺ｨ險ｭ險亥峙繧定ｦ区ｯ斐∋繧・
            // 繝ｭ繝懊ャ繝医繰SON縺ｫ moveSpeed 縺後≠繧九◇縲ゅｈ縺励》emp縺ｮ0繝槭せ逶ｮ縺ｫ荳頑嶌縺阪□・√・
            // 繝ｭ繝懊ャ繝医繰SON縺ｫ attackRange 縺ｯ...譖ｸ縺九ｌ縺ｦ縺ｪ縺・↑縲ゅ§繧・≠菴輔ｂ縺励↑縺・ｼ医せ繝ｫ繝ｼ・会ｼ√・
            Deserialize(j, temp);

            // 3. 螳梧・縺励◆邂ｱ繧偵√ご繝ｼ繝縺ｮ荳也阜縺ｫ逋ｻ骭ｲ縺吶ｋ
            world->template AddComponent<T>(entity, std::move(temp));
        };
        // 竊摘CS繧ｨ繝ｳ繧ｸ繝ｳ縺ｮ豁｣縺励＞TypeID蜿門ｾ玲婿豕輔↓菫ｮ豁｣
        registry.RegisterInternal(CCL::ECS::TypeInfo<T>::ID(), name, std::move(info));
    }
} // namespace ComponentMetaJson