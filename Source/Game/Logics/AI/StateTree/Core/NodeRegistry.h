/// @file  NodeRegistry.h
/// @brief StateTree ノード種別を「名前 → ID → 実体 / メタ情報」で解決するレジストリ。
///        （M2 改訂版：paramsサイズ・instanceサイズ・フィールド記述子を型から自動収集）
///
/// 改訂点:
///   - NodeDesc に paramsSize / paramFields を追加
///   - RegisterTask<T> / RegisterCondition<T> が T::Params と T::ParamFields() を
///     if constexpr で検出して NodeDesc に格納（Params を持たないノードは 0 扱い）
///   - typeId / 名前から NodeDesc を引くアクセサを追加（ローダ・エディタが使う）
#pragma once
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "StateTreeNode.h"
#include "Game/Logics/AI/StateTree/Core/StateTreeAsset.h"
#include "NodeParamField.h"


namespace CCL::AI
{
    // ====================================================================
    // 型特性: T が ネスト型 Params を持つか
    // ====================================================================
    template<class T, class = void> struct st_has_params : std::false_type {};
    template<class T>
    struct st_has_params<T, std::void_t<typename T::Params>> : std::true_type {};


    // ====================================================================
    // NodeDesc — ノード種別のメタ情報（ローダ・エディタが参照）
    // ====================================================================
    struct NodeDesc
    {
        NodeTypeID  typeId = ST_INVALID_NODE_TYPE;
        std::string name;        ///< JSON/エディタの識別名
        std::string category;    ///< エディタのグループ分け
        uint32_t    paramsSize = 0;  ///< sizeof(T::Params)（無ければ 0）
        std::vector<NodeParamField> paramFields;  ///< Params の各フィールド記述
    };


    // ====================================================================
    // NodeRegistry
    // ====================================================================
    class NodeRegistry
    {
    public:
        static NodeRegistry& Instance()
        {
            static NodeRegistry reg;
            return reg;
        }

        // ----------------------------------------------------------------
        // 登録
        // ----------------------------------------------------------------
        template<typename T>
        NodeTypeID RegisterTask(const std::string& name,
            const std::string& category = "General")
        {
            const NodeTypeID id = static_cast<NodeTypeID>(m_taskEntries.size());

            TaskEntry entry;
            entry.instance = std::make_unique<T>();

            NodeDesc desc;
            desc.typeId = id;
            desc.name = name;
            desc.category = category;
            FillParamMeta<T>(desc);     // Params があれば paramsSize / paramFields を埋める
            entry.desc = desc;

            m_taskNameToId[name] = id;
            m_taskEntries.push_back(std::move(entry));
            return id;
        }

        template<typename T>
        NodeTypeID RegisterCondition(const std::string& name,
            const std::string& category = "General")
        {
            const NodeTypeID id = static_cast<NodeTypeID>(m_condEntries.size());

            CondEntry entry;
            entry.instance = std::make_unique<T>();

            NodeDesc desc;
            desc.typeId = id;
            desc.name = name;
            desc.category = category;
            FillParamMeta<T>(desc);     // 条件も Params を持てる
            entry.desc = desc;

            m_condNameToId[name] = id;
            m_condEntries.push_back(std::move(entry));
            return id;
        }

        // ----------------------------------------------------------------
        // 実行時アクセス
        // ----------------------------------------------------------------
        ITask* GetTask(NodeTypeID id) const
        {
            return (id < m_taskEntries.size()) ? m_taskEntries[id].instance.get() : nullptr;
        }

        ICondition* GetCondition(NodeTypeID id) const
        {
            return (id < m_condEntries.size()) ? m_condEntries[id].instance.get() : nullptr;
        }

        // ----------------------------------------------------------------
        // メタ情報アクセス（ローダ・エディタ用）
        // ----------------------------------------------------------------
        const NodeDesc* GetTaskDesc(NodeTypeID id) const
        {
            return (id < m_taskEntries.size()) ? &m_taskEntries[id].desc : nullptr;
        }

        const NodeDesc* GetConditionDesc(NodeTypeID id) const
        {
            return (id < m_condEntries.size()) ? &m_condEntries[id].desc : nullptr;
        }

        NodeTypeID FindTaskByName(const std::string& name) const
        {
            auto it = m_taskNameToId.find(name);
            return (it != m_taskNameToId.end()) ? it->second : ST_INVALID_NODE_TYPE;
        }
        NodeTypeID FindConditionByName(const std::string& name) const
        {
            auto it = m_condNameToId.find(name);
            return (it != m_condNameToId.end()) ? it->second : ST_INVALID_NODE_TYPE;
        }

        const NodeDesc* FindTaskDescByName(const std::string& name) const
        {
            auto id = FindTaskByName(name); return (id != ST_INVALID_NODE_TYPE) ? GetTaskDesc(id) : nullptr;
        }

        const NodeDesc* FindConditionDescByName(const std::string& name) const
        {
            auto id = FindConditionByName(name); return (id != ST_INVALID_NODE_TYPE) ? GetConditionDesc(id) : nullptr;
        }

        // ----------------------------------------------------------------
        // 一覧（エディタのパレット）
        // ----------------------------------------------------------------
        std::vector<NodeDesc> GetAllTaskDescs() const
        {
            std::vector<NodeDesc> v; v.reserve(m_taskEntries.size());
            for (auto& e : m_taskEntries) v.push_back(e.desc);
            return v;
        }
        std::vector<NodeDesc> GetAllConditionDescs() const
        {
            std::vector<NodeDesc> v; v.reserve(m_condEntries.size());
            for (auto& e : m_condEntries) v.push_back(e.desc);
            return v;
        }

    private:
        NodeRegistry() = default;
        NodeRegistry(const NodeRegistry&) = delete;
        NodeRegistry& operator=(const NodeRegistry&) = delete;

        /// @brief T::Params があれば paramsSize と paramFields を desc に詰める。
        template<typename T>
        static void FillParamMeta(NodeDesc& desc)
        {
            if constexpr (st_has_params<T>::value) {
                desc.paramsSize = sizeof(typename T::Params);
                desc.paramFields = T::ParamFields();   // Params を持つなら ParamFields() 必須
            }
            else {
                desc.paramsSize = 0;
            }
        }

        struct TaskEntry { std::unique_ptr<ITask>      instance; NodeDesc desc; };
        struct CondEntry { std::unique_ptr<ICondition> instance; NodeDesc desc; };

        std::vector<TaskEntry> m_taskEntries;
        std::vector<CondEntry> m_condEntries;
        std::unordered_map<std::string, NodeTypeID> m_taskNameToId;
        std::unordered_map<std::string, NodeTypeID> m_condNameToId;
    };

} // namespace CCL::AI