#pragma once
// ===========================================================================
// 概要: ActionID から BT のアクション・条件のインスタンスを引くレジストリ（シングルトン）。
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

        // --- 登録 ---
        template <typename T>
        void RegisterAction(ActionID id)
        {
            if (id >= m_actions.size()) m_actions.resize(id + 1);
            m_actions[id] = std::make_unique<T>();
            AccumulateAccess(*m_actions[id]);
        }

        template <typename T>
        void RegisterCondition(ActionID id)
        {
            if (id >= m_conditions.size()) m_conditions.resize(id + 1);
            m_conditions[id] = std::make_unique<T>();
            AccumulateAccess(*m_conditions[id]);
        }

        // コンストラクタ引数が要るアクション（TriggeredMotionAction など）を、作ったものごと登録する。
        void RegisterActionInstance(ActionID id, std::unique_ptr<IBTAction> action)
        {
            if (id >= m_actions.size()) m_actions.resize(id + 1);
            m_actions[id] = std::move(action);
            AccumulateAccess(*m_actions[id]);
        }

        // --- 解決 ---
        IBTAction    *GetAction(ActionID id) const { return (id < m_actions.size()) ? m_actions[id].get() : nullptr; }
        IBTCondition *GetCondition(ActionID id) const { return (id < m_conditions.size()) ? m_conditions[id].get() : nullptr; }

        // --- アクセス申告をまとめた結果（BehaviorTreeSystem がスケジューラへ渡す）---
        //
        // 登録済みの全アクション・条件が「world から直接触る」と申告した型の和集合。
        // BehaviorTreeSystem 自身の Read<>/Write<> には現れない読み書きを、ここで申告に載せる。
        const std::vector<TypeID> &ExtraWriteTypes() const { return m_extraWrites; }
        const std::vector<TypeID> &ExtraReadTypes() const { return m_extraReads; }

        // その ID の行動・条件が登録されているか（起動時の突き合わせ用）。
        bool HasAction(ActionID id) const { return GetAction(id) != nullptr; }
        bool HasCondition(ActionID id) const { return GetCondition(id) != nullptr; }

    private:
        BTActionRegistry()                                    = default;
        BTActionRegistry(const BTActionRegistry &)            = delete;
        BTActionRegistry &operator=(const BTActionRegistry &) = delete;

        /**
         * @brief 申告を和集合として溜める。
         *
         * 件数が数個なので線形探索で足りる（登録は起動時に1回だけで、実行中には呼ばれない）。
         *
         * @param dst 溜める先。src のうち dst に無い型を足す。
         * @param src 足す申告。
         */
        static void Merge(std::vector<TypeID> &dst, const std::vector<TypeID> &src)
        {
            for (TypeID t : src)
                if (std::find(dst.begin(), dst.end(), t) == dst.end()) dst.push_back(t);
        }

        template <typename T>
        void AccumulateAccess(const T &node)
        {
            Merge(m_extraWrites, node.GetExtraWriteTypes());
            Merge(m_extraReads, node.GetExtraReadTypes());
        }

        std::vector<std::unique_ptr<IBTAction>>    m_actions;
        std::vector<std::unique_ptr<IBTCondition>> m_conditions;

        std::vector<TypeID> m_extraWrites;
        std::vector<TypeID> m_extraReads;
    };

} // namespace CCL::AI::BT
