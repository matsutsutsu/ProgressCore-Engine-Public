// ActionID → アクション/条件クラスを解決するレジストリ
#pragma once
#include <algorithm>
#include <memory>
#include <vector>

#include "BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"


namespace CCL::AI::BT
{
// BTActionRegistry は、BTのアクション・条件を ActionID から解決するための辞書
    class BTActionRegistry
    {
    public:
        using TypeID = CCL::ECS::TypeID;

        static BTActionRegistry& Instance()
        {
            static BTActionRegistry reg;
            return reg;
        }

        // --- 登録 ---
        template<typename T>
        void RegisterAction(ActionID id)
        {
            if (id >= m_actions.size()) m_actions.resize(id + 1);
            m_actions[id] = std::make_unique<T>();
            AccumulateAccess(*m_actions[id]);
        }

        template<typename T>
        void RegisterCondition(ActionID id)
        {
            if (id >= m_conditions.size()) m_conditions.resize(id + 1);
            m_conditions[id] = std::make_unique<T>();
            AccumulateAccess(*m_conditions[id]);
        }

        // コンストラクタ引数が要るアクション（Wait等）を構築済みで登録。
        void RegisterActionInstance(ActionID id, std::unique_ptr<IBTAction> action)
        {
            if (id >= m_actions.size()) m_actions.resize(id + 1);
            m_actions[id] = std::move(action);
            AccumulateAccess(*m_actions[id]);
        }

        // --- 解決 ---
        IBTAction*    GetAction(ActionID id)    const { return (id < m_actions.size())    ? m_actions[id].get()    : nullptr; }
        IBTCondition* GetCondition(ActionID id) const { return (id < m_conditions.size()) ? m_conditions[id].get() : nullptr; }

        // --- アクセス申告の集約結果（BehaviorTreeSystem がスケジューラへ中継する）---
        //
        // 登録済みの全アクション・条件が「world から直接触る」と申告した型の和集合。
        // BTエンジン自身の Read<>/Write<> には現れない書き込みを、ここで表に出す。
        const std::vector<TypeID>& ExtraWriteTypes() const { return m_extraWrites; }
        const std::vector<TypeID>& ExtraReadTypes()  const { return m_extraReads;  }

        // 登録されている行動・条件の数を数える（起動時の整合チェック用）。
        bool HasAction(ActionID id)    const { return GetAction(id)    != nullptr; }
        bool HasCondition(ActionID id) const { return GetCondition(id) != nullptr; }


    private:
        BTActionRegistry() = default;
        BTActionRegistry(const BTActionRegistry&) = delete;
        BTActionRegistry& operator=(const BTActionRegistry&) = delete;

        // 申告を和集合として溜める。件数が数個なので線形探索で足りる
        // （登録は起動時に1回だけで、実行時には呼ばれない）。
        static void Merge(std::vector<TypeID> &dst, const std::vector<TypeID> &src)
        {
            for (TypeID t : src)
                if (std::find(dst.begin(), dst.end(), t) == dst.end()) dst.push_back(t);
        }

        template <typename T>
        void AccumulateAccess(const T &node)
        {
            Merge(m_extraWrites, node.GetExtraWriteTypes());
            Merge(m_extraReads,  node.GetExtraReadTypes());
        }

        std::vector<std::unique_ptr<IBTAction>>    m_actions;
        std::vector<std::unique_ptr<IBTCondition>> m_conditions;

        std::vector<TypeID> m_extraWrites;
        std::vector<TypeID> m_extraReads;
    };

} 
