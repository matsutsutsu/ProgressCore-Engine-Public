#pragma once
// ===========================================================================
// 概要: ActionID から BT のアクション・条件のインスタンスを引くレジストリ（シングルトン）
//
// 責務:
//   - アクションと条件を ActionID ごとに1つずつ登録する
//   - ActionID から登録済みのインスタンスを返す
//   - 各アクション・条件が申告した Read/Write の型を和集合にまとめる
// ===========================================================================
#include <algorithm>
#include <memory>
#include <vector>

#include "BTAction.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"

namespace CCL::AI::BT
{
    class BTActionRegistry
    {
    public:
        using TypeID = CCL::ECS::TypeID;

        static BTActionRegistry &Instance()
        {
            static BTActionRegistry reg;
            return reg;
        }

        // --- 登録（起動時だけ。評価中に呼ぶと、並列に読んでいる配列が伸びる） ---

        // 行動 T を作って id に登録する
        template <typename T>
        void RegisterAction(ActionID id)
        {
            if (id >= m_actions.size()) m_actions.resize(id + 1);
            m_actions[id] = std::make_unique<T>();
            AccumulateAccess(*m_actions[id]);
        }

        // 条件 T を作って id に登録する
        template <typename T>
        void RegisterCondition(ActionID id)
        {
            if (id >= m_conditions.size()) m_conditions.resize(id + 1);
            m_conditions[id] = std::make_unique<T>();
            AccumulateAccess(*m_conditions[id]);
        }

        // コンストラクタ引数が要るアクションを、作ったものごと登録する（所有権はレジストリへ移る）
        void RegisterActionInstance(ActionID id, std::unique_ptr<IBTAction> action)
        {
            if (id >= m_actions.size()) m_actions.resize(id + 1);
            m_actions[id] = std::move(action);
            AccumulateAccess(*m_actions[id]);
        }

        // --- 解決 ---
        IBTAction    *GetAction(ActionID id) const { return (id < m_actions.size()) ? m_actions[id].get() : nullptr; }          // 未登録なら nullptr
        IBTCondition *GetCondition(ActionID id) const { return (id < m_conditions.size()) ? m_conditions[id].get() : nullptr; } // 未登録なら nullptr

        // --- アクセス申告（全登録分の和集合。BehaviorTreeSystem がスケジューラへ渡す） ---
        const std::vector<TypeID> &ExtraWriteTypes() const { return m_extraWrites; }
        const std::vector<TypeID> &ExtraReadTypes() const { return m_extraReads; }

        bool HasAction(ActionID id) const { return GetAction(id) != nullptr; }
        bool HasCondition(ActionID id) const { return GetCondition(id) != nullptr; }

    private:
        BTActionRegistry()                                    = default;
        BTActionRegistry(const BTActionRegistry &)            = delete;
        BTActionRegistry &operator=(const BTActionRegistry &) = delete;

        // src のうち dst にない型を dst へ足す（件数が数個で、起動時にしか呼ばないので線形探索）
        static void Merge(std::vector<TypeID> &dst, const std::vector<TypeID> &src)
        {
            for (TypeID t : src)
                if (std::find(dst.begin(), dst.end(), t) == dst.end()) dst.push_back(t);
        }

        // 登録した行動・条件（T は IBTAction か IBTCondition）の申告を、全体の申告に足す
        template <typename T>
        void AccumulateAccess(const T &node)
        {
            Merge(m_extraWrites, node.GetExtraWriteTypes());
            Merge(m_extraReads, node.GetExtraReadTypes());
        }

        std::vector<std::unique_ptr<IBTAction>>    m_actions;    // ActionID を添字にした行動。未登録の添字は nullptr
        std::vector<std::unique_ptr<IBTCondition>> m_conditions; // ActionID を添字にした条件。未登録の添字は nullptr

        std::vector<TypeID> m_extraWrites; // 全登録分の Write 申告の和集合
        std::vector<TypeID> m_extraReads;  // 全登録分の Read 申告の和集合
    };

} // namespace CCL::AI::BT
