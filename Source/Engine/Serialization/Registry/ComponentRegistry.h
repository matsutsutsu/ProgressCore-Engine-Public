// =====================================================================
// ComponentRegistry.h
//   コンポーネント型 ↔ 文字列名のマッピングと、Save/Load 関数の保持。
//
//   リファクタリング方針:
//     - シリアライズは ComponentMetaJson::ComponentSerializer<T> 一本に統一。
//       Register<T> から旧 HasCustomSerialize 分岐を撤去 (経路が分岐しない)。
//     - 旧 RemapFunc / HasRemap / MetaInfo::remap (生 EntityID 時代の遺物) を撤去。
//       エンティティ参照の付け替えは FieldKind::EntityLink が自動で行うため不要。
//     - ImGui 関連は ComponentRegistry_Editor.h に分離済み (本ヘッダは引かない)。
// =====================================================================
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Serialization/Core/SaveContext.h"
#include "Engine/Serialization/Core/LoadContext.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

// ==========================================================
// SFINAE: ComponentMeta<T>::isSerializable の検出
//   特殊化が isSerializable を持たない場合は「保存対象 (true)」とみなす。
// ==========================================================
template <typename T, typename = void>
struct HasIsSerializable : std::true_type {};

template <typename T>
struct HasIsSerializable<T, std::void_t<decltype(ComponentMeta<T>::isSerializable)>>
    : std::bool_constant<ComponentMeta<T>::isSerializable> {
};


class ComponentRegistry {
public:
    using SerializeFunc = std::function<void(nlohmann::json&, const void*, CCL::Serialization::SaveContext*)>;
    using DeserializeToWorldFunc = std::function<void(CCL::ECS::World*, CCL::ECS::EntityID,
        const nlohmann::json&, CCL::Serialization::LoadContext*)>;

    struct MetaInfo
    {
        std::string            name;
        SerializeFunc          serialize;          // (json, comp*, SaveContext) -> void
        DeserializeToWorldFunc deserializeToWorld; // (world, entity, json, LoadContext) -> void

        // false の型はシーン/プレハブ保存からスキップされる (実行時専用コンポーネント等)
        bool isSerializable = true;
    };

    static ComponentRegistry& Instance();

    const std::unordered_map<std::string, MetaInfo>& GetAllMeta() const { return m_nameToMeta; }

    template <typename T>
    void Register(const std::string &name)
    {
        MetaInfo info;
        info.name = name;

        // ---- 保存: SaveContext を渡す (EntityLink が自動 remap される) ----
        info.serialize = [](nlohmann::json &j, const void *ptr, CCL::Serialization::SaveContext *ctx) {
            ComponentMetaJson::ComponentSerializer<T>::Save(j, *static_cast<const T *>(ptr), ctx);
        };

        // ---- 復元: LoadContext を渡す (EntityLink が自動 Resolve される) ----
        info.deserializeToWorld = [](CCL::ECS::World            *world,
                                      CCL::ECS::EntityID               entity,
                                      const nlohmann::json            &j,
                                      CCL::Serialization::LoadContext *ctx) {
            T temp{};
            ComponentMetaJson::ComponentSerializer<T>::Load(j, temp, ctx);
            world->AddComponent<T>(entity, std::move(temp));
        };

        // ---- 保存可否フラグ (実際の保存経路がこの値を見てスキップする) ----
        //   ComponentMeta<T>::isSerializable が無ければ true、false 指定なら false。
        info.isSerializable = HasIsSerializable<T>::value;

        m_nameToMeta[name]                          = std::move(info);
        m_typeIdToName[CCL::ECS::TypeInfo<T>::ID()] = name;
    }

    const MetaInfo* GetInfoByName(const std::string& name) const {
        auto it = m_nameToMeta.find(name);
        return (it != m_nameToMeta.end()) ? &it->second : nullptr;
    }

    std::string GetNameByTypeID(CCL::ECS::TypeID id) const {
        auto it = m_typeIdToName.find(id);
        return (it != m_typeIdToName.end()) ? it->second : std::string{};
    }


private:
    std::unordered_map<std::string, MetaInfo> m_nameToMeta;
    std::unordered_map<CCL::ECS::TypeID, std::string> m_typeIdToName;
};


// =====================================================================
// REGISTER_COMPONENT マクロ
//   - リンカ最適化 (/OPT:REF) で消されない強制保護
//   - 診断ログは NDEBUG で消える (リリースビルドではゼロコスト)
//   - エディタ向け ImGui 登録は REGISTER_COMPONENT_EDITOR_GUI に分離
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

// コンポーネント自動登録時の診断ログ（[CCL_DIAG]/[CCL_DIAG_ID]）は不要になったため無効化。
// 再度追跡したくなったら、下を Debug 限定の OutputDebugStringA 実装に差し戻せばよい。
#define _CCL_DIAG_REG(Name, T) ((void)0)

#define REGISTER_COMPONENT(T, Name)                                                               \
    namespace {                                                                                   \
        struct AutoReg_##T {                                                                      \
            AutoReg_##T() {                                                                       \
                _CCL_DIAG_REG(Name, T);                                                           \
                if (!ComponentRegistry::Instance().GetInfoByName(Name)) {                         \
                    ComponentRegistry::Instance().Register<T>(Name);                              \
                }                                                                                 \
            }                                                                                     \
        };                                                                                        \
    }                                                                                             \
    extern "C" _CCL_USED int g_MetaAnchor_##T = (AutoReg_##T{}, 1);                               \
    _CCL_LINK(g_MetaAnchor_##T)