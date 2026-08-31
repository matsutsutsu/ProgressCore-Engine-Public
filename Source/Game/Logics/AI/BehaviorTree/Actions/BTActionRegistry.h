/**
 * ActionID → アクション/条件クラスを解決するレジストリ。switch廃止の核。
 */
#pragma once
#include <memory>
#include <vector>

#include "BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"


namespace CCL::AI::BT
{
    class BTActionRegistry
    {
    public:
        static BTActionRegistry& Instance()
        {
            static BTActionRegistry reg;
            return reg;
        }

        // --- 登録（既存 ActionRegistry.h の ID をそのままキーにする）---
        template<typename T>
        void RegisterAction(ActionID id)
        {
            if (id >= m_actions.size()) m_actions.resize(id + 1);
            m_actions[id] = std::make_unique<T>();
        }

        template<typename T>
        void RegisterCondition(ActionID id)
        {
            if (id >= m_conditions.size()) m_conditions.resize(id + 1);
            m_conditions[id] = std::make_unique<T>();
        }

        /// コンストラクタ引数が要るアクション（Wait等）を構築済みで登録。
        void RegisterActionInstance(ActionID id, std::unique_ptr<IBTAction> action)
        {
            if (id >= m_actions.size()) m_actions.resize(id + 1);
            m_actions[id] = std::move(action);
        }

        // --- 解決 ---
        IBTAction*    GetAction(ActionID id)    const { return (id < m_actions.size())    ? m_actions[id].get()    : nullptr; }
        IBTCondition* GetCondition(ActionID id) const { return (id < m_conditions.size()) ? m_conditions[id].get() : nullptr; }


    private:
        BTActionRegistry() = default;
        BTActionRegistry(const BTActionRegistry&) = delete;
        BTActionRegistry& operator=(const BTActionRegistry&) = delete;

        std::vector<std::unique_ptr<IBTAction>>    m_actions;
        std::vector<std::unique_ptr<IBTCondition>> m_conditions;
    };

} // namespace CCL::AI::BT
