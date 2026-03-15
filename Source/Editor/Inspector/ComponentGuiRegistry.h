#pragma once
#include <functional>
#include <unordered_map>
#include <string>
#include "ECS/Common/CCL_Common.h"

// Worldクラスの前方宣言 (ヘッダ読み込み負荷軽減のため)
namespace CCL::ECS::Core { class World; }

using namespace CCL::ECS;

// コンポーネントごとのGUI描画ロジックと表示名を管理する
class ComponentGuiRegistry {
public:
    // EntityID と World* を受け取るように変更
    using GuiFunc = std::function<void(void*, EntityID, CCL::ECS::Core::World*)>;
    using CtorFunc = std::function<void(void*)>; // 追加：初期化用

    // 名前と関数をセットで保持する構造体
    struct Entry {
        std::string name;
        GuiFunc guiFunc = nullptr;
        CtorFunc ctor = nullptr;
        size_t size = 0;  
        // これがないと、Inspectorから追加したコンポーネントがメモリリークします
        Destructor dtor;

        TypeData::ConstructFunc constructor = nullptr;
        TypeData::AssignFunc    assigner    = nullptr;
        TypeData::MoveFunc      mover       = nullptr;

        // WorldとEntityIDだけで描画を完結させるためのラッパー関数
        // InspectorWindowはこれを使うことで、void* へのキャストやGetComponentを自分でしなくて済む
        std::function<void(CCL::ECS::Core::World *, EntityID)> drawInspector = nullptr;
    };

    void Initialize();

    // コンストラクタで初期化を呼ぶようにすると使い勝手が良い
    ComponentGuiRegistry() {
        Initialize();
    }



    // T: 型, name: 表示名, func: 描画関数
    template <typename T> void Register(const std::string &name, GuiFunc func)
    {
        Entry entry;
        entry.name    = name;
        entry.guiFunc = func;
        entry.ctor    = [](void *ptr) { new (ptr) T(); };
        entry.size    = sizeof(T);

        // TypeDataを作って全情報を取得
        TypeData td;
        td.Create<T>();

        entry.dtor        = td.destructor;
        entry.constructor = td.constructor;
        entry.assigner    = td.assigner;
        entry.mover       = td.mover;

        // ★ここで「安全な描画関数」を生成して保存する
        // 実行時に T型 の GetComponent を行い、ポインタが有効な場合のみ guiFunc を呼ぶ
        entry.drawInspector = [func](CCL::ECS::Core::World *world, EntityID entity) {
            // World経由で型安全に取得
            T *component = world->GetComponent<T>(entity);
            if (component) {
                // 生ポインタに変換して従来のGUI関数に渡す
                func(static_cast<void *>(component), entity, world);
            }
        };

        _registry[CCL::ECS::TypeInfo<T>::ID()] = entry;
    }

    // getter追加
    const Entry& GetEntry(TypeID id) const { return _registry.at(id); }

    // IDから登録名を取得（InspectorWindowで使用）
    std::string GetName(TypeID id) {
        auto it = _registry.find(id);
        if (it != _registry.end()) return it->second.name;
        return "Unknown Component (ID: " + std::to_string(id) + ")";
    }

    // 登録されているすべてのTypeIDをリストで取得（コンポーネント追加用）
    std::vector<TypeID> GetAllTypeIDs() const {
        std::vector<TypeID> ids;
        ids.reserve(_registry.size());
        for (const auto& [id, entry] : _registry) {
            ids.push_back(id);
        }

        // UIでの表示順を安定させるため、IDまたは名前でソートすることをお勧めします
        std::sort(ids.begin(), ids.end(), [this](TypeID a, TypeID b) {
            return _registry.at(a).name < _registry.at(b).name;
            });

        return ids;
    }

    // エディタから呼び出すための簡略化メソッド
    void DrawInspector(TypeID id, CCL::ECS::Core::World *world, EntityID entity)
    {
        auto it = _registry.find(id);
        if (it != _registry.end() && it->second.drawInspector) {
            // 見つかったら、ComponentGuiRegistryで予約しておいた「機能」を実行！
            it->second.drawInspector(world, entity);
        }
    }

    // 描画実行
    void Draw(TypeID id, void* data, EntityID entity, CCL::ECS::Core::World* world);

private:
    std::unordered_map<TypeID, Entry> _registry;
};