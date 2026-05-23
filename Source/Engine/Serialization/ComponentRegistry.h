// =====================================================================
// ComponentRegistry.h
//   コンポーネント型 ↔ 文字列名のマッピングと、Save/Load 関数の保持。
//
//   旧版からの主な変更:
//     1. ImGui 関連 include を削除 (→ ComponentRegistry_Editor.h に分離)
//        これによりランタイムビルドからエディタコードを物理的に除外できる。
//     2. SerializeFunc / DeserializeToWorldFunc が SaveContext/LoadContext を受け取る
//        ハードコード分岐を排除する核心部分。
//     3. <json.hpp>, <Windows.h> はヘッダから追放 (前方宣言 + .cpp に隔離)
//        ビルド時間の大幅短縮が目的。
//
//   注意: REGISTER_COMPONENT マクロ自体は OutputDebugString を含むため
//         #ifndef NDEBUG ガードを入れた。リリースビルドでは診断ログが消える。
// =====================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "ECS/Core/CCL_World.h"
#include "Engine/Serialization/SaveContext.h"
#include "Engine/Serialization/LoadContext.h"

// HasCustomSerialize は ComponentMetaJson.h で定義されている。
// ここでは前方宣言だけ参照する。
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

// ==========================================================
// SFINAE: ComponentMeta<T> の isSerializable / Remap 検出
// ==========================================================
template <typename T, typename = void>
struct HasIsSerializable : std::true_type {};

template <typename T>
struct HasIsSerializable<T, std::void_t<decltype(ComponentMeta<T>::isSerializable)>>
    : std::bool_constant<ComponentMeta<T>::isSerializable> {
};

template <typename T, typename = void>
struct HasRemap : std::false_type {};

template <typename T>
struct HasRemap<T, std::void_t<decltype(ComponentMeta<T>::Remap(
    std::declval<T&>(),
    std::declval<const std::unordered_map<uint64_t, CCL::ECS::EntityID>&>()))>>
    : std::true_type {};


class ComponentRegistry {
public:
    // ★ シグネチャ変更: Context を受け取る形に統一
    using SerializeFunc = std::function<void(nlohmann::json&, const void*, CCL::Serialization::SaveContext*)>;
    using DeserializeToWorldFunc = std::function<void(CCL::ECS::Core::World*, CCL::ECS::EntityID, const nlohmann::json&, CCL::Serialization::LoadContext*)>;

    // [旧] カスタム Remap 関数 (旧版互換用、新規コードでは不要)
    using RemapFunc = std::function<void(CCL::ECS::Core::World*, CCL::ECS::EntityID,
        const std::unordered_map<uint64_t, CCL::ECS::EntityID>&)>;

    struct MetaInfo {
        std::string name;
        SerializeFunc serialize;
        DeserializeToWorldFunc deserializeToWorld;
        RemapFunc remap = nullptr;

        std::function<bool(CCL::ECS::Core::World*, CCL::ECS::EntityID)> hasComponent = nullptr;
        std::function<void(CCL::ECS::Core::World*, CCL::ECS::EntityID, nlohmann::json&, CCL::Serialization::SaveContext*)> saveToJSON = nullptr;
    };

    static ComponentRegistry& Instance();

    const std::unordered_map<std::string, MetaInfo>& GetAllMeta() const { return _nameToMeta; }

    template <typename T>
    void Register(const std::string& name)
    {
        MetaInfo info;
        info.name = name;

        // 1. Save: SaveContext を渡して自動 remap
        info.serialize = [](nlohmann::json& j, const void* ptr, CCL::Serialization::SaveContext* ctx) {
            const T* component = static_cast<const T*>(ptr);
            ComponentMetaJson::Serialize(j, *component, ctx);
            };

        // 2. Load: LoadContext を渡して自動 remap
        info.deserializeToWorld = [](CCL::ECS::Core::World* world,
            CCL::ECS::EntityID entity,
            const nlohmann::json& j,
            CCL::Serialization::LoadContext* ctx)
            {
                if constexpr (ComponentMetaJson::HasCustomSerialize<T>::value) {
                    // 注意: カスタム実装側も Context 対応シグネチャに合わせること。
                    //       移行期は古いシグネチャもサポートする adapter を書いても良い。
                    ComponentMeta<T>::deserializeToWorld(world, entity, j, ctx);
                }
                else {
                    T temp{};
                    ComponentMetaJson::Deserialize(j, temp, ctx);
                    world->AddComponent<T>(entity, std::move(temp));
                }
            };

        // 3. [旧] カスタム Remap (互換用)
        if constexpr (HasRemap<T>::value) {
            info.remap = [](CCL::ECS::Core::World* world, CCL::ECS::EntityID entity,
                const std::unordered_map<uint64_t, CCL::ECS::EntityID>& idMap)
                {
                    if (auto* comp = world->GetComponent<T>(entity)) {
                        ComponentMeta<T>::Remap(*comp, idMap);
                    }
                };
        }

        // 4. Scene Save 用 (isSerializable で除外する型をフィルタ)
        if constexpr (HasIsSerializable<T>::value) {
            info.hasComponent = [](CCL::ECS::Core::World* w, CCL::ECS::EntityID e) {
                return w->HasComponent<T>(e);
                };

            info.saveToJSON = [](CCL::ECS::Core::World* w, CCL::ECS::EntityID e,
                nlohmann::json& j, CCL::Serialization::SaveContext* ctx)
                {
                    if (auto* comp = w->GetComponent<T>(e)) {
                        nlohmann::json compJson;
                        ComponentMetaJson::Serialize(compJson, *comp, ctx);
                        j[ComponentMeta<T>::displayName] = compJson;
                    }
                };
        }

        _nameToMeta[name] = std::move(info);
        _typeIdToName[CCL::ECS::TypeInfo<T>::ID()] = name;
    }

    const MetaInfo* GetInfoByName(const std::string& name) const {
        auto it = _nameToMeta.find(name);
        return (it != _nameToMeta.end()) ? &it->second : nullptr;
    }

    std::string GetNameByTypeID(CCL::ECS::TypeID id) const {
        auto it = _typeIdToName.find(id);
        return (it != _typeIdToName.end()) ? it->second : std::string{};
    }

    void RegisterInternal(CCL::ECS::TypeID typeId, const std::string& name, MetaInfo&& info)
    {
        _nameToMeta[name] = std::move(info);
        _typeIdToName[typeId] = name;
    }

private:
    std::unordered_map<std::string, MetaInfo> _nameToMeta;
    std::unordered_map<CCL::ECS::TypeID, std::string> _typeIdToName;
};


// =====================================================================
// REGISTER_COMPONENT マクロ
//   - リンカ最適化 (/OPT:REF) で消されない強制保護
//   - 診断ログは NDEBUG で消える (リリースビルドではゼロコスト)
//   - エディタ向け ImGui 登録は別マクロ REGISTER_COMPONENT_EDITOR_GUI に分離した
//     (ComponentRegistry_Editor.h)
// =====================================================================
#ifdef _MSC_VER
#define _CCL_LINK_IMPL(sym) __pragma(comment(linker, "/include:" #sym))
#define _CCL_LINK(sym)      _CCL_LINK_IMPL(sym)
#define _CCL_USED
#elif defined(__GNUC__) || defined(__clang__)
#define _CCL_LINK(sym)
#define _CCL_USED __attribute__((used))
#else
#define _CCL_LINK(sym)
#define _CCL_USED
#endif

// 診断ログ: デバッグビルドだけ。リリースで sprintf_s と OutputDebugString は完全に消える。
#ifndef NDEBUG
#include <Windows.h>  // ★ここに移動: マクロ展開された側 (.cpp) でのみ取り込まれる
#include <cstdio>
#define _CCL_DIAG_REG(Name, T)                                                                    \
        do {                                                                                          \
            char _buf[256];                                                                           \
            bool _alreadyReg = (ComponentRegistry::Instance().GetInfoByName(Name) != nullptr);        \
            sprintf_s(_buf, "[CCL_DIAG] AutoReg<%s> already=%d\n", Name, _alreadyReg);                \
            OutputDebugStringA(_buf);                                                                 \
            sprintf_s(_buf, "[CCL_DIAG_ID] %s -> TypeInfo::ID() = %llu\n",                            \
                Name, (unsigned long long)CCL::ECS::TypeInfo<T>::ID());                               \
            OutputDebugStringA(_buf);                                                                 \
        } while(0)
#else
#define _CCL_DIAG_REG(Name, T) ((void)0)
#endif

// 注意: ImGui 用の登録は REGISTER_COMPONENT_EDITOR_GUI(T) を別途呼ぶ運用にした。
//       これによりランタイム専用のビルド単位は ImGui を一切引かなくて済む。
#define REGISTER_COMPONENT(T, Name)                                                                   \
    namespace {                                                                                       \
        struct AutoReg_##T {                                                                          \
            AutoReg_##T() {                                                                           \
                _CCL_DIAG_REG(Name, T);                                                               \
                if (!ComponentRegistry::Instance().GetInfoByName(Name)) {                             \
                    ComponentRegistry::Instance().Register<T>(Name);                                  \
                }                                                                                     \
            }                                                                                         \
        };                                                                                            \
    }                                                                                                 \
    extern "C" _CCL_USED int g_MetaAnchor_##T = (AutoReg_##T{}, 1);                                   \
    _CCL_LINK(g_MetaAnchor_##T)