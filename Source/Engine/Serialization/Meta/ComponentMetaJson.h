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

    inline void SerializeField(json &j, const FieldDescriptor &fd, const void *obj)
    {
        if (!fd.serialize) return;
        using namespace DirectX;
        using namespace _MetaJsonDX;
        switch (fd.kind) {
        case FieldKind::Float: j[fd.name] = RawField<float>(obj, fd.offset); break;
        case FieldKind::Int: j[fd.name] = RawField<int>(obj, fd.offset); break;
        case FieldKind::Bool: j[fd.name] = RawField<bool>(obj, fd.offset); break;
        case FieldKind::UInt8:
            j[fd.name] = static_cast<int>(RawField<uint8_t>(obj, fd.offset));
            break;
        case FieldKind::EnumU8:
            j[fd.name] = static_cast<int>(RawField<uint8_t>(obj, fd.offset));
            break;
        case FieldKind::EnumInt: j[fd.name] = RawField<int>(obj, fd.offset); break;
        case FieldKind::Float2: {
            json jv;
            F2_to(jv, RawField<XMFLOAT2>(obj, fd.offset));
            j[fd.name] = std::move(jv);
            break;
        }
        case FieldKind::Float3: {
            json jv;
            F3_to(jv, RawField<XMFLOAT3>(obj, fd.offset));
            j[fd.name] = std::move(jv);
            break;
        }
        case FieldKind::Float4: {
            json jv;
            F4_to(jv, RawField<XMFLOAT4>(obj, fd.offset));
            j[fd.name] = std::move(jv);
            break;
        }
        case FieldKind::String: j[fd.name] = RawField<std::string>(obj, fd.offset); break;
        default: break;
        }
    }

    inline void DeserializeField(const json &j, const FieldDescriptor &fd, void *obj)
    {
        if (!fd.serialize) return;
        if (!j.contains(fd.name)) return;
        using namespace DirectX;
        using namespace _MetaJsonDX;
        const json &jv = j.at(fd.name);
        switch (fd.kind) {
        case FieldKind::Float: RawField<float>(obj, fd.offset) = jv.get<float>(); break;
        case FieldKind::Int: RawField<int>(obj, fd.offset) = jv.get<int>(); break;
        case FieldKind::Bool: RawField<bool>(obj, fd.offset) = jv.get<bool>(); break;
        case FieldKind::UInt8:
            RawField<uint8_t>(obj, fd.offset) = static_cast<uint8_t>(jv.get<int>());
            break;
        case FieldKind::EnumU8:
            RawField<uint8_t>(obj, fd.offset) = static_cast<uint8_t>(jv.get<int>());
            break;
        case FieldKind::EnumInt: RawField<int>(obj, fd.offset) = jv.get<int>(); break;
        case FieldKind::Float2: F2_from(jv, RawField<XMFLOAT2>(obj, fd.offset)); break;
        case FieldKind::Float3: F3_from(jv, RawField<XMFLOAT3>(obj, fd.offset)); break;
        case FieldKind::Float4: F4_from(jv, RawField<XMFLOAT4>(obj, fd.offset)); break;
        case FieldKind::String:
            RawField<std::string>(obj, fd.offset) = jv.get<std::string>();
            break;
        default: break;
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