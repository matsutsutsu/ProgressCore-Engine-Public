#pragma once
// ===========================================================================
// 概要: コンポーネントの型と保存名の対応、保存・読込関数を持つレジストリ ComponentRegistry
//
// 責務:
//   - 型 ID ⇔ 保存名の対応と、型ごとの保存・読込関数を保持する（シングルトン）
//   - REGISTER_COMPONENT マクロで、各コンポーネントを静的初期化時に登録する
//
// 備考: エディタ用の GUI 登録は ComponentRegistry_Editor.h に分けている（このヘッダは ImGui を使わない）
// ===========================================================================
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Serialization/Core/SaveContext.h"
#include "Engine/Serialization/Core/LoadContext.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

// ComponentMeta<T>::isSerializable の値（持っていなければ true）
template <typename T, typename = void>
struct HasIsSerializable : std::true_type
{
};

template <typename T>
struct HasIsSerializable<T, std::void_t<decltype(ComponentMeta<T>::isSerializable)>>
    : std::bool_constant<ComponentMeta<T>::isSerializable>
{
};

class ComponentRegistry
{
public:
    using SerializeFunc          = std::function<void(nlohmann::json &, const void *, CCL::Serialization::SaveContext *)>;
    using DeserializeToWorldFunc = std::function<void(CCL::ECS::World *, CCL::ECS::EntityID,
                                                      const nlohmann::json &, CCL::Serialization::LoadContext *)>;

    struct MetaInfo
    {
        std::string            name;
        SerializeFunc          serialize;             // コンポーネントを JSON に書く
        DeserializeToWorldFunc deserializeToWorld;    // JSON から作って Entity に AddComponent する
        bool                   isSerializable = true; // false ならシーン・プレハブに保存しない（実行時専用のコンポーネント等）
    };

    static ComponentRegistry &Instance();

    const std::unordered_map<std::string, MetaInfo> &GetAllMeta() const { return m_nameToMeta; }

    template <typename T>
    void Register(const std::string &name)
    {
        MetaInfo info;
        info.name = name;

        // SaveContext を渡すので、EntityLink はローカルインデックスに変換される
        info.serialize = [](nlohmann::json &j, const void *ptr, CCL::Serialization::SaveContext *ctx) {
            ComponentMetaJson::ComponentSerializer<T>::Save(j, *static_cast<const T *>(ptr), ctx);
        };

        // LoadContext を渡すので、EntityLink は新しい EntityID に変換される
        info.deserializeToWorld = [](CCL::ECS::World                 *world,
                                     CCL::ECS::EntityID               entity,
                                     const nlohmann::json            &j,
                                     CCL::Serialization::LoadContext *ctx) {
            T temp{};
            ComponentMetaJson::ComponentSerializer<T>::Load(j, temp, ctx);
            world->AddComponent<T>(entity, std::move(temp));
        };

        info.isSerializable = HasIsSerializable<T>::value;

        m_nameToMeta[name]                          = std::move(info);
        m_typeIdToName[CCL::ECS::TypeInfo<T>::ID()] = name;
    }

    const MetaInfo *GetInfoByName(const std::string &name) const
    {
        auto it = m_nameToMeta.find(name);
        return (it != m_nameToMeta.end()) ? &it->second : nullptr;
    }

    std::string GetNameByTypeID(CCL::ECS::TypeID id) const
    {
        auto it = m_typeIdToName.find(id);
        return (it != m_typeIdToName.end()) ? it->second : std::string{};
    }

private:
    std::unordered_map<std::string, MetaInfo>         m_nameToMeta;
    std::unordered_map<CCL::ECS::TypeID, std::string> m_typeIdToName;
};

// ===========================================================================
// REGISTER_COMPONENT(T, Name)
//   静的初期化で ComponentRegistry に T を登録する
//   登録用の変数はどこからも参照されないので、リンカの最適化（/OPT:REF）で
//   消されないよう /include で残す
// ===========================================================================
#ifdef _MSC_VER
#define _CCL_LINK_IMPL(sym) __pragma(comment(linker, "/include:" #sym))
#define _CCL_LINK(sym) _CCL_LINK_IMPL(sym)
#define _CCL_USED
#elif defined(__GNUC__) || defined(__clang__)
#define _CCL_LINK(sym)
#define _CCL_USED __attribute__((used))
#else
#define _CCL_LINK(sym)
#define _CCL_USED
#endif

#define REGISTER_COMPONENT(T, Name)                                     \
    namespace                                                           \
    {                                                                   \
        struct AutoReg_##T                                              \
        {                                                               \
            AutoReg_##T()                                               \
            {                                                           \
                if (!ComponentRegistry::Instance().GetInfoByName(Name)) \
                {                                                       \
                    ComponentRegistry::Instance().Register<T>(Name);    \
                }                                                       \
            }                                                           \
        };                                                              \
    }                                                                   \
    extern "C" _CCL_USED int g_MetaAnchor_##T = (AutoReg_##T{}, 1);     \
    _CCL_LINK(g_MetaAnchor_##T)