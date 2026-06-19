/// @file  NodeRegistry.h
/// @brief StateTree ノードの種別を「名前 → ID → 実体」で解決するレジストリ。
///
/// ここが設計の接合点:
///   - C++ ハードコード（M1）: RegisterTask<T>() で種別を登録し typeId を受け取る
///   - JSON ローダ（M2）     : FindByName("MoveToTarget") → typeId でマッピング
///   - エディタ（M3）        : GetAllTaskDescs() でパレットを自動生成
///
/// 3つのフェーズ全てが「このレジストリ1か所を見る」設計にすることで、
/// 新ノード追加時に RegisterTask を1行書くだけで全フェーズに伝播する。
///
/// 使用例:
/// @code
///   // アプリ起動時（or Scene初期化時）に登録
///   auto& reg = NodeRegistry::Instance();
///   reg.RegisterTask<MoveToTargetTask>("MoveToTarget");
///   reg.RegisterCondition<IsTargetInRangeCondition>("IsTargetInRange");
///
///   // StateTreeSystem が実行時に使用
///   ITask* task = reg.GetTask(typeId);
///   task->Tick(ctx);
///
///   // JSON ローダ（M2）が使用
///   NodeTypeID id = reg.FindTaskByName("MoveToTarget");
/// @endcode
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "StateTreeNode.h"   // ITask / ICondition / NodeTypeID
#include "StateTreeAsset.h"  // NodeTypeID の実体はここで定義


namespace CCL::AI
{
    // ====================================================================
    // ノード記述子（エディタのパレット表示・JSON ヘルプ用）
    // ====================================================================

    /// @brief レジストリに登録されたノードのメタ情報。
    ///        エディタが「使えるタスク一覧」を表示するために使う。
    struct NodeDesc
    {
        NodeTypeID  typeId;
        std::string name;       ///< JSON/エディタで使う識別名（"MoveToTarget" 等）
        std::string category;   ///< エディタのグループ分け（"Movement", "Combat" 等）
        uint32_t    paramsSize; ///< Params 構造体のバイト数（JSON デフォルト生成用）
    };


    // ====================================================================
    // NodeRegistry
    // ====================================================================

    /// @brief タスク・条件の種別を管理するシングルトンレジストリ。
    ///
    /// @note シングルトンだが World Resource ではない。
    ///       StateTreeAsset の「型情報」なので World のライフサイクルをまたぐ。
    ///       アプリ起動時に1度だけ登録すれば良い。
    class NodeRegistry
    {
    public:
        // ---------------------------------------------------------------
        // アクセサ
        // ---------------------------------------------------------------

        static NodeRegistry& Instance()
        {
            static NodeRegistry reg;
            return reg;
        }

        // ---------------------------------------------------------------
        // タスク登録
        // ---------------------------------------------------------------

        /// @brief タスク種別を登録し、種別 ID を返す。
        ///
        /// @tparam T       ITask を継承したタスク実装クラス。
        ///                 T::Params 型を持つことを推奨（JSON ローダ用）。
        /// @param name     JSON / エディタで使う識別名。一意でなければならない。
        /// @param category エディタのグループ名（省略可）
        /// @return         発行された NodeTypeID（StateTreeAsset::AddTask に渡す）
        template<typename T>
        NodeTypeID RegisterTask(const std::string& name,
                                const std::string& category = "General")
        {
            const NodeTypeID id = static_cast<NodeTypeID>(_taskEntries.size());

            TaskEntry entry;
            entry.instance = std::make_unique<T>();

            NodeDesc desc;
            desc.typeId     = id;
            desc.name       = name;
            desc.category   = category;
            desc.paramsSize = entry.instance->InstanceDataSize(); // 参考値
            entry.desc      = desc;

            _taskNameToId[name] = id;
            _taskEntries.push_back(std::move(entry));
            return id;
        }

        // ---------------------------------------------------------------
        // 条件登録
        // ---------------------------------------------------------------

        /// @brief 条件種別を登録し、種別 ID を返す。
        template<typename T>
        NodeTypeID RegisterCondition(const std::string& name,
                                     const std::string& category = "General")
        {
            const NodeTypeID id = static_cast<NodeTypeID>(_condEntries.size());

            CondEntry entry;
            entry.instance = std::make_unique<T>();

            NodeDesc desc;
            desc.typeId     = id;
            desc.name       = name;
            desc.category   = category;
            desc.paramsSize = 0;
            entry.desc      = desc;

            _condNameToId[name] = id;
            _condEntries.push_back(std::move(entry));
            return id;
        }

        // ---------------------------------------------------------------
        // 実行時アクセス（StateTreeSystem から呼ばれる）
        // ---------------------------------------------------------------

        /// @brief タスク実体を取得。typeId が無効なら nullptr。
        ITask* GetTask(NodeTypeID id) const
        {
            if (id >= _taskEntries.size()) return nullptr;
            return _taskEntries[id].instance.get();
        }

        /// @brief 条件実体を取得。typeId が無効なら nullptr。
        ICondition* GetCondition(NodeTypeID id) const
        {
            if (id >= _condEntries.size()) return nullptr;
            return _condEntries[id].instance.get();
        }

        // ---------------------------------------------------------------
        // 名前解決（JSON ローダ・M2 から使う）
        // ---------------------------------------------------------------

        /// @brief タスク名 → typeId。見つからなければ ST_INVALID_NODE_TYPE。
        NodeTypeID FindTaskByName(const std::string& name) const
        {
            auto it = _taskNameToId.find(name);
            return (it != _taskNameToId.end()) ? it->second : ST_INVALID_NODE_TYPE;
        }

        /// @brief 条件名 → typeId。見つからなければ ST_INVALID_NODE_TYPE。
        NodeTypeID FindConditionByName(const std::string& name) const
        {
            auto it = _condNameToId.find(name);
            return (it != _condNameToId.end()) ? it->second : ST_INVALID_NODE_TYPE;
        }

        // ---------------------------------------------------------------
        // エディタ用（M3 のパレット生成）
        // ---------------------------------------------------------------

        const std::vector<NodeDesc>& GetAllTaskDescs() const
        {
            // 毎回再構築を避けるためキャッシュする
            if (_taskDescCache.size() != _taskEntries.size())
            {
                _taskDescCache.clear();
                for (const auto& e : _taskEntries)
                    _taskDescCache.push_back(e.desc);
            }
            return _taskDescCache;
        }

        const std::vector<NodeDesc>& GetAllConditionDescs() const
        {
            if (_condDescCache.size() != _condEntries.size())
            {
                _condDescCache.clear();
                for (const auto& e : _condEntries)
                    _condDescCache.push_back(e.desc);
            }
            return _condDescCache;
        }

    private:
        NodeRegistry() = default;
        NodeRegistry(const NodeRegistry&) = delete;
        NodeRegistry& operator=(const NodeRegistry&) = delete;

        // ---------------------------------------------------------------
        // 内部エントリ
        // ---------------------------------------------------------------

        struct TaskEntry
        {
            std::unique_ptr<ITask> instance;
            NodeDesc               desc;
        };

        struct CondEntry
        {
            std::unique_ptr<ICondition> instance;
            NodeDesc                    desc;
        };

        std::vector<TaskEntry>   _taskEntries;
        std::vector<CondEntry>   _condEntries;

        std::unordered_map<std::string, NodeTypeID> _taskNameToId;
        std::unordered_map<std::string, NodeTypeID> _condNameToId;

        mutable std::vector<NodeDesc> _taskDescCache;
        mutable std::vector<NodeDesc> _condDescCache;
    };

} // namespace CCL::AI
