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
template <typename T> struct AutoComponentRegister {
    AutoComponentRegister(const std::string &name)
    {
        // 重複登録を防ぐためのガード（念のため）
        if (!ComponentRegistry::Instance().GetInfoByName(name)) {
            ComponentRegistry::Instance().Register<T>(name);
        }

        // 2. ImGui(インスペクタ)用の登録
        ComponentMetaImGui::RegisterGuiMeta<T>(ComponentGuiRegistry::Instance());
    }
};

// ★これを使うマクロ
// C++17の inline 変数を使うことで、ヘッダーに書いても多重定義エラーにならず、
// プログラム起動時に自動的に実行されます。
#define REGISTER_COMPONENT(T, Name)                                                                \
    inline static AutoComponentRegister<T> g_auto_register_##T(Name);