/// @file  BTActionRegistry.h
/// @brief ActionID → アクション/条件クラスを解決するレジストリ。switch廃止の核。
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
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
        void RegisterAction(ActionID id, const std::string& name = "")
        {
            if (id >= _actions.size()) _actions.resize(id + 1);
            _actions[id] = std::make_unique<T>();
            if (!name.empty()) _actionNames[name] = id;
        }

        template<typename T>
        void RegisterCondition(ActionID id, const std::string& name = "")
        {
            if (id >= _conditions.size()) _conditions.resize(id + 1);
            _conditions[id] = std::make_unique<T>();
            if (!name.empty()) _conditionNames[name] = id;
        }

        /// @brief コンストラクタ引数が要るアクション（Wait等）を構築済みで登録。
        void RegisterActionInstance(ActionID id, std::unique_ptr<IBTAction> action,
                                    const std::string& name = "")
        {
            if (id >= _actions.size()) _actions.resize(id + 1);
            _actions[id] = std::move(action);
            if (!name.empty()) _actionNames[name] = id;
        }

        // --- 解決 ---
        IBTAction*    GetAction(ActionID id)    const { return (id < _actions.size())    ? _actions[id].get()    : nullptr; }
        IBTCondition* GetCondition(ActionID id) const { return (id < _conditions.size()) ? _conditions[id].get() : nullptr; }

        bool HasAction(ActionID id)    const { return id < _actions.size()    && _actions[id]    != nullptr; }
        bool HasCondition(ActionID id) const { return id < _conditions.size() && _conditions[id] != nullptr; }

    private:
        BTActionRegistry() = default;
        BTActionRegistry(const BTActionRegistry&) = delete;
        BTActionRegistry& operator=(const BTActionRegistry&) = delete;

        std::vector<std::unique_ptr<IBTAction>>    _actions;
        std::vector<std::unique_ptr<IBTCondition>> _conditions;
        std::unordered_map<std::string, ActionID>  _actionNames;
        std::unordered_map<std::string, ActionID>  _conditionNames;
    };

} // namespace CCL::AI::BT
