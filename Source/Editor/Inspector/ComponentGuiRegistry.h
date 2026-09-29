#pragma once
// ===========================================================================
// 概要: コンポーネントの型ごとに、インスペクタの描画関数と表示情報を持つレジストリ。
//
// 責務:
//   - 型ごとに表示名・カテゴリ・並び順・非表示フラグ・描画関数を登録する
//   - インスペクタからコンポーネントを追加するための生成・破棄・コピー関数を持つ
//   - TypeID から登録内容を引く
//
// 備考: 登録は静的初期化（各 *Meta.cpp）から行われ、その時点では TypeID がまだ
//       確定していない場合がある。そのため type_index をキーに保存し、TypeID との
//       対応付けは最初に引かれたとき（ResolveBindings）にまとめて行う。
// ===========================================================================
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <string>
#include <typeindex>
#include <vector>
#include "Engine/ECS/Common/CCL_TypeErasure.h"
#include "Engine/ECS/Common/CCL_TypeInfo.h"
#include "Engine/ECS/Core/CCL_World.h"

using namespace CCL::ECS;

class ComponentGuiRegistry
{
public:
    // 実体（関数内の static 変数）は .cpp の1か所に置く
    static ComponentGuiRegistry &Instance();

    using GuiFunc  = std::function<void(void *, EntityID, CCL::ECS::World *)>; // (コンポーネント, 対象, World)
    using CtorFunc = std::function<void(void *)>;                              // 確保済みメモリに既定値で構築する

    // 型1つぶんの登録内容
    struct Entry
    {
        std::string name;
        std::string category     = "Misc"; // インスペクタのカテゴリ見出し
        int         sortPriority = 1000;   // 小さいほど先頭
        bool        hidden       = false;  // true でインスペクタ非表示
        GuiFunc     guiFunc      = nullptr;
        CtorFunc    ctor         = nullptr;
        size_t      size         = 0;

        // Inspector から追加したコンポーネントを、エンティティの削除時に正しく破棄するためのデストラクタ
        Destructor dtor;

        TypeData::ConstructFunc constructor = nullptr;
        TypeData::AssignFunc    assigner    = nullptr;
        TypeData::MoveFunc      mover       = nullptr;

        // World と EntityID だけで描画するためのラッパー。
        // InspectorWindow は void* へのキャストや GetComponent を自分で行わずに済む
        std::function<void(CCL::ECS::World *, EntityID)> drawInspector = nullptr;
    };

    ComponentGuiRegistry() = default;

    /**
     * @brief 型 T を登録する。
     * @tparam T 登録するコンポーネントの型
     * @param name 表示名
     * @param category インスペクタのカテゴリ見出し
     * @param sortPriority 並び順（小さいほど先頭）
     * @param hidden true ならインスペクタに出さない
     * @param func 描画関数
     */
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

        // 生成・破棄・コピー・ムーブの関数は TypeData から取る
        TypeData td;
        td.Create<T>();

        entry.dtor        = td.destructor;
        entry.constructor = td.constructor;
        entry.assigner    = td.assigner;
        entry.mover       = td.mover;

        // 実行時に T の GetComponent を行い、取れた場合だけ func を呼ぶ描画関数を作る
        entry.drawInspector = [func](CCL::ECS::World *world, EntityID entity) {
            T *component = world->GetComponent<T>(entity);
            if (component)
            {
                func(static_cast<void *>(component), entity, world);
            }
        };

        std::type_index key = typeid(T);

        // insert で登録する（同じ型を2回登録した場合は最初の登録が残る）
        m_registry.insert({key, entry});

        // TypeID との対応付けは ResolveBindings まで遅らせる
        m_pendingBindings.push_back([this, key]() {
            m_typeIdToIndex.insert({CCL::ECS::TypeInfo<T>::ID(), key});
        });
    }

    // 保留していた TypeID との対応付けをまとめて行う。各取得関数の先頭で呼ぶ（2回目以降は何もしない）
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

    // 未登録の id を渡すと例外（std::out_of_range）になる
    const Entry &GetEntry(TypeID id)
    {
        ResolveBindings();
        return m_registry.at(m_typeIdToIndex.at(id));
    }

    // 表示名。未登録なら "Unknown Component (ID: n)"
    std::string GetName(TypeID id)
    {
        ResolveBindings();
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

    // 並び順（小さいほど先頭）。未登録は最後。
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

    // entity が持つ id のコンポーネントを描く。未登録なら何もしない
    void DrawInspector(TypeID id, CCL::ECS::World *world, EntityID entity)
    {
        ResolveBindings();
        auto idIt = m_typeIdToIndex.find(id);
        if (idIt == m_typeIdToIndex.end()) return;
        auto it = m_registry.find(idIt->second);
        if (it != m_registry.end() && it->second.drawInspector)
        {
            it->second.drawInspector(world, entity);
        }
    }

    // 登録済みの全 TypeID（表示名の順）
    std::vector<TypeID> GetAllTypeIDs()
    {
        ResolveBindings();
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

    // 表示名の参照を返す。未登録なら空文字列。
    //   GetName と違ってコピーせず、未登録を空文字列で判定できる
    const std::string &GetComponentName(TypeID id)
    {
        ResolveBindings();

        auto idIt = m_typeIdToIndex.find(id);
        if (idIt != m_typeIdToIndex.end())
        {
            auto regIt = m_registry.find(idIt->second);
            if (regIt != m_registry.end())
            {
                return regIt->second.name;
            }
        }

        static const std::string unknown = "";
        return unknown;
    }

    // data（id のコンポーネントの実体）を描く。未登録ならその旨を表示する
    void Draw(TypeID id, void *data, EntityID entity, CCL::ECS::World *world);

private:
    std::unordered_map<std::type_index, Entry>  m_registry;      // 本体。型から決まる type_index をキーにする
    std::unordered_map<TypeID, std::type_index> m_typeIdToIndex; // TypeID → type_index（ResolveBindings で作る）

    std::vector<std::function<void()>> m_pendingBindings;    // TypeID との対応付けの保留分
    bool                               m_isResolved = false; // 保留分を処理済みか
};
