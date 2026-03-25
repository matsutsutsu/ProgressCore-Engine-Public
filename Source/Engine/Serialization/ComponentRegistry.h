#pragma once
#include <functional>
#include <unordered_map>
#include <string>
#include "ECS/Core/CCL_World.h"
#include <json.hpp>
//#include "ComponentSerializers.h"

// 古い手動シリアライザの代わりに、強力な自動シリアライザを読み込む
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

// ImGui(インスペクタ)の自動登録に必要なヘッダーをコアで読み込む
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"

class ComponentRegistry {
public:
    // JSONへ書き出す関数
    using SerializeFunc = std::function<void(nlohmann::json&, const void*)>;
    // JSONからWorldへ復元する関数
    using DeserializeToWorldFunc = std::function<void(CCL::ECS::Core::World*, CCL::ECS::EntityID, const nlohmann::json&)>;

    struct MetaInfo {
        std::string name;
        SerializeFunc serialize;
        DeserializeToWorldFunc deserializeToWorld;
    };

    static ComponentRegistry &Instance();

    // 型Tを登録する
    template <typename T>
    void Register(const std::string& name) {
        MetaInfo info;
        info.name = name;

        // [Save用] void* を T* にキャストして to_json
        info.serialize = [](nlohmann::json& j, const void* ptr) {
            const T* component = static_cast<const T*>(ptr);

            ComponentMetaJson::Serialize(j, *component);
            };

        // [Load用] JSONから一時変数Tを作成し、World::RequestAddComponent<T> にコピーして渡す
        info.deserializeToWorld = [](CCL::ECS::Core::World* world, CCL::ECS::EntityID entity, const nlohmann::json& j) {
            T temp{}; // ★波括弧で確実に初期化する

            ComponentMetaJson::Deserialize(j, temp);

            world->AddComponent<T>(entity, std::move(temp));
            };

        // マップ登録
        _nameToMeta[name] = info;

        // TypeID -> Name
        auto typeId = CCL::ECS::TypeInfo<T>::ID();
        _typeIdToName[typeId] = name;
    }

    const MetaInfo* GetInfoByName(const std::string& name) {
        auto it = _nameToMeta.find(name);
        return (it != _nameToMeta.end()) ? &it->second : nullptr;
    }

    std::string GetNameByTypeID(CCL::ECS::TypeID id) {
        auto it = _typeIdToName.find(id);
        return (it != _typeIdToName.end()) ? it->second : "";
    }

    // 【重要】実装を登録するための非テンプレート関数
    void RegisterInternal(CCL::ECS::TypeID typeId, const std::string &name, MetaInfo &&info)
    {
        _nameToMeta[name]     = info;
        _typeIdToName[typeId] = name;
    }

private:
    std::unordered_map<std::string, MetaInfo> _nameToMeta;
    std::unordered_map<CCL::ECS::TypeID, std::string> _typeIdToName;
};


// 自動登録用のヘルパー構造体
//template <typename T> struct AutoComponentRegister {
//    AutoComponentRegister(const std::string &name)
//    {
//        // 重複登録を防ぐためのガード（念のため）
//        if (!ComponentRegistry::Instance().GetInfoByName(name)) {
//            ComponentRegistry::Instance().Register<T>(name);
//        }
//
//        // 2. ImGui(インスペクタ)用の登録
//        ComponentMetaImGui::RegisterGuiMeta<T>(ComponentGuiRegistry::Instance());
//    }
//};

// ── リンカ強制保護 (究極版：COMDAT最適化の完全破壊) ────────────────────────
// 登録用変数だけが最適化で切り捨てられるのを防ぐため、
// エクスポート保護されたアンカー変数の中でラムダ式を回し、登録処理を道連れにする。
// ─────────────────────────────────────────────────────────────────────


// ── リンカ強制保護 (確実版) ────────────────────────────────────────────────
// __declspec(dllexport) は EXE ビルドで /OPT:REF に対して不安定。
// #pragma comment(linker, "/include:X") はリンカの参照グラフに
// 直接シンボルを追加する唯一の確実な方法。
// ─────────────────────────────────────────────────────────────────────────

#ifdef _MSC_VER
    // 2段マクロ展開: T が完全展開された後に文字列化する
#define _CCL_LINK_IMPL(sym) __pragma(comment(linker, "/include:" #sym))
#define _CCL_LINK(sym)      _CCL_LINK_IMPL(sym)
#define _CCL_USED
#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang: コンパイル段階で削除を阻止する
#define _CCL_LINK(sym)
#define _CCL_USED __attribute__((used))
#else
#define _CCL_LINK(sym)
#define _CCL_USED
#endif

#define REGISTER_COMPONENT(T, Name)                                                         \
    namespace {                                                                             \
        struct AutoReg_##T {                                                                \
            AutoReg_##T() {                                                                 \
                if (!ComponentRegistry::Instance().GetInfoByName(Name)) {                   \
                    ComponentRegistry::Instance().Register<T>(Name);                        \
                }                                                                           \
                ComponentMetaImGui::RegisterGuiMeta<T>(ComponentGuiRegistry::Instance());   \
            }                                                                               \
        };                                                                                  \
    }                                                                                       \
    /* ★修正点1: dllexport を廃止し、pragma で確実に参照を付与         */               \
    /* ★修正点2: static ローカルを廃止し、初期化式に直接コンストラクタを呼ぶ */          \
    /*   (AutoReg_##T{}, 1) = 一時オブジェクトを構築→登録実行→即破棄、値は 1 */         \
    extern "C" _CCL_USED int g_MetaAnchor_##T = (AutoReg_##T{}, 1);                        \
    _CCL_LINK(g_MetaAnchor_##T)