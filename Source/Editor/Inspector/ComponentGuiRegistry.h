#pragma once
#include <functional>
#include <unordered_map>
#include <string>
#include <typeindex>
#include <vector>
#include "Engine/ECS/Common/CCL_TypeErasure.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/ECS/Core/CCL_World.h"

using namespace CCL::ECS;

// コンポーネントごとのGUI描画ロジックと表示名を管理する
class ComponentGuiRegistry
{
public:
    // シングルトンアクセス
    // 中身を消し、宣言のみにする
    static ComponentGuiRegistry &Instance();

    // EntityID と World* を受け取るように変更
    using GuiFunc  = std::function<void(void *, EntityID, CCL::ECS::World *)>;
    using CtorFunc = std::function<void(void *)>; // 追加：初期化用

    // 名前と関数をセットで保持する構造体
    struct Entry
    {
        std::string name;
        std::string category     = "Misc"; // インスペクタのカテゴリ見出し
        int         sortPriority = 1000;   // 小さいほど先頭
        bool        hidden       = false;  // true でインスペクタ非表示
        GuiFunc     guiFunc      = nullptr;
        CtorFunc    ctor         = nullptr;
        size_t      size         = 0;
        // これがないと、Inspectorから追加したコンポーネントがメモリリークします
        Destructor dtor;

        TypeData::ConstructFunc constructor = nullptr;
        TypeData::AssignFunc    assigner    = nullptr;
        TypeData::MoveFunc      mover       = nullptr;

        // WorldとEntityIDだけで描画を完結させるためのラッパー関数
        // InspectorWindowはこれを使うことで、void* へのキャストやGetComponentを自分でしなくて済む
        std::function<void(CCL::ECS::World *, EntityID)> drawInspector = nullptr;
    };

    // コンストラクタで初期化を呼ぶようにすると使い勝手が良い
    ComponentGuiRegistry() = default;

    // T: 型, name: 表示名, func: 描画関数
    template <typename T>
    void Register(const std::string &name, const std::string &category,
                  int sortPriority, bool hidden, GuiFunc func)
    {
        Entry entry;
        entry.name         = name;
        entry.category     = category;
        entry.sortPriority = sortPriority;
        entry.hidden       = hidden;
        entry.guiFunc      = func;
        entry.ctor         = [](void *ptr) { new (ptr) T(); };
        entry.size         = sizeof(T);

        // TypeDataを作って全情報を取得
        TypeData td;
        td.Create<T>();

        entry.dtor        = td.destructor;
        entry.constructor = td.constructor;
        entry.assigner    = td.assigner;
        entry.mover       = td.mover;

        // ここで「安全な描画関数」を生成して保存する
        // 実行時に T型 の GetComponent を行い、ポインタが有効な場合のみ guiFunc を呼ぶ
        entry.drawInspector = [func](CCL::ECS::World *world, EntityID entity) {
            // World経由で型安全に取得
            T *component = world->GetComponent<T>(entity);
            if (component)
            {
                // 生ポインタに変換して従来のGUI関数に渡す
                func(static_cast<void *>(component), entity, world);
            }
        };

        std::type_index key = typeid(T);

        //  operator[] は使わず insert で安全に登録（コンパイルエラー解決）
        m_registry.insert({key, entry});

        // TypeID の取得を「実行時」まで遅延させる
        m_pendingBindings.push_back([this, key]() {
            m_typeIdToIndex.insert({CCL::ECS::TypeInfo<T>::ID(), key});
        });
    }

    // 描画前に必ず実行され、保留されていたID紐づけを一気に解決する
    void ResolveBindings()
    {
        if (m_isResolved) return;
        for (auto &bindFunc : m_pendingBindings)
        {
            bindFunc();
        }
        m_pendingBindings.clear();
        m_isResolved = true;
    }

    const Entry &GetEntry(TypeID id)
    {
        ResolveBindings(); // 取得前に解決
        return m_registry.at(m_typeIdToIndex.at(id));
    }

    std::string GetName(TypeID id)
    {
        ResolveBindings(); // 取得前に解決
        auto idIt = m_typeIdToIndex.find(id);
        if (idIt == m_typeIdToIndex.end())
            return "Unknown Component (ID: " + std::to_string(id) + ")";
        auto it = m_registry.find(idIt->second);
        if (it != m_registry.end()) return it->second.name;
        return "Unknown Component (ID: " + std::to_string(id) + ")";
    }

    // カテゴリ見出し名。未登録は "Misc"。
    std::string GetCategory(TypeID id)
    {
        ResolveBindings();
        auto idIt = m_typeIdToIndex.find(id);
        if (idIt == m_typeIdToIndex.end()) return "Misc";
        auto it = m_registry.find(idIt->second);
        if (it != m_registry.end() && !it->second.category.empty())
            return it->second.category;
        return "Misc";
    }

    // 並び順 (小さいほど先頭)。未登録は最後方。
    int GetSortPriority(TypeID id)
    {
        ResolveBindings();
        auto idIt = m_typeIdToIndex.find(id);
        if (idIt == m_typeIdToIndex.end()) return 100000;
        auto it = m_registry.find(idIt->second);
        return (it != m_registry.end()) ? it->second.sortPriority : 100000;
    }

    // インスペクタ非表示か
    bool IsHidden(TypeID id)
    {
        ResolveBindings();
        auto idIt = m_typeIdToIndex.find(id);
        if (idIt == m_typeIdToIndex.end()) return false;
        auto it = m_registry.find(idIt->second);
        return (it != m_registry.end()) ? it->second.hidden : false;
    }

    void DrawInspector(TypeID id, CCL::ECS::World *world, EntityID entity)
    {
        ResolveBindings(); // 描画前に解決
        auto idIt = m_typeIdToIndex.find(id);
        if (idIt == m_typeIdToIndex.end()) return;
        auto it = m_registry.find(idIt->second);
        if (it != m_registry.end() && it->second.drawInspector)
        {
            it->second.drawInspector(world, entity);
        }
    }

    std::vector<TypeID> GetAllTypeIDs()
    {
        ResolveBindings(); // 取得前に解決
        std::vector<TypeID> ids;
        ids.reserve(m_typeIdToIndex.size());
        for (const auto &[id, _] : m_typeIdToIndex)
        {
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), [this](TypeID a, TypeID b) {
            auto ia = m_typeIdToIndex.at(a);
            auto ib = m_typeIdToIndex.at(b);
            return m_registry.at(ia).name < m_registry.at(ib).name;
        });
        return ids;
    }

    // =========================================================
    //  TypeID からコンポーネント名を取得する逆引き関数
    // =========================================================
    const std::string &GetComponentName(TypeID id)
    {
        ResolveBindings(); // 取得前に未解決のバインディングを処理

        auto idIt = m_typeIdToIndex.find(id);
        if (idIt != m_typeIdToIndex.end())
        {
            auto regIt = m_registry.find(idIt->second);
            if (regIt != m_registry.end())
            {
                return regIt->second.name; // 既に保存されている名前を返す！
            }
        }

        // 見つからなかった場合用の空文字
        static const std::string unknown = "";
        return unknown;
    }

    // 描画実行
    void Draw(TypeID id, void *data, EntityID entity, CCL::ECS::World *world);

private:
    // メインストレージ: type_index → Entry（型に紐づく不変キー）
    std::unordered_map<std::type_index, Entry> m_registry;
    // 逆引きマップ: TypeID → type_index（実行時に確定するIDから引く用）
    std::unordered_map<TypeID, std::type_index> m_typeIdToIndex;

    // 遅延評価（Lazy Binding）用の変数群
    std::vector<std::function<void()>> m_pendingBindings;
    bool                               m_isResolved = false;
};
