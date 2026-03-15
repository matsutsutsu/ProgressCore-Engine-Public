#pragma once
#include <functional>
#include <unordered_map>
#include <string>
#include "ECS/Core/CCL_World.h"
#include <json.hpp>
//#include "ComponentSerializers.h"



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

    static ComponentRegistry& Instance() { static ComponentRegistry s; return s; }

    // 型Tを登録する
    template <typename T>
    void Register(const std::string& name) {
        MetaInfo info;
        info.name = name;

        // [Save用] void* を T* にキャストして to_json
        info.serialize = [](nlohmann::json& j, const void* ptr) {
            const T* component = static_cast<const T*>(ptr);

            SerializeComponent(j, *component);
            };

        // [Load用] JSONから一時変数Tを作成し、World::RequestAddComponent<T> にコピーして渡す
        info.deserializeToWorld = [](CCL::ECS::Core::World* world, CCL::ECS::EntityID entity, const nlohmann::json& j) {
            // 1. 一時変数を作成（ここでデフォルトコンストラクタが走る -> shared_ptrはnullptrになる）
            T tempComponent;
            
            // 2. JSONから値をロード（SetModelなどでshared_ptrに値が入る）
            DeserializeComponent(j, tempComponent);

            // 3. Worldにコピーして追加（AddComponent<T> がデストラクタ登録まで全部やってくれる）
            world->AddComponent<T>(entity, std::move(tempComponent));
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
    }
};

// ★これを使うマクロ
// C++17の inline 変数を使うことで、ヘッダーに書いても多重定義エラーにならず、
// プログラム起動時に自動的に実行されます。
#define REGISTER_COMPONENT(T, Name)                                                                \
    inline static AutoComponentRegister<T> g_auto_register_##T(Name);