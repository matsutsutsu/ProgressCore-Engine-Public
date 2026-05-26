/**
 * @file   CCL_TaskScheduler.cpp
 * @brief  TaskSchedulerの実装
 *
 * @details
 *  BuildGraph / Execute / 衝突判定の実装。旧版でヘッダにあったログスパムを除去し、
 *  デフォルトでは静かに動作するようにしています。デバッグログが必要な場合は
 *  外部から ConsoleLogger 等を差し込む方式を推奨します。
 *
 * @author CCL ECS Library
 */
#include "CCL_TaskScheduler.h"
#include "../Core/CCL_JobSystem.h"

#include <algorithm>


namespace CCL::ECS
{
    // =========================================================================
    //  衝突判定ヘルパ
    // =========================================================================

    bool TaskScheduler::Contains(const std::vector<TypeID>& list, TypeID id) noexcept
    {
        for (TypeID v : list) if (v == id) return true;
        return false;
    }


    bool TaskScheduler::IsConflict(const SystemBase* a, const SystemBase* b)
    {
        if (!a || !b) return false;

        const auto aWrites = a->GetWriteTypes();
        const auto bWrites = b->GetWriteTypes();
        const auto aReads  = a->GetReadTypes();
        const auto bReads  = b->GetReadTypes();

        // Write × Write
        for (TypeID id : aWrites) {
            if (Contains(bWrites, id)) return true;
        }
        // Write × Read
        for (TypeID id : aWrites) {
            if (Contains(bReads, id)) return true;
        }
        for (TypeID id : bWrites) {
            if (Contains(aReads, id)) return true;
        }
        return false;
    }


    // =========================================================================
    //  BuildGraph
    // =========================================================================

    void TaskScheduler::BuildGraph(
        const std::vector<std::unique_ptr<SystemBase>>& systems)
    {
        _executionBatches.clear();

        // priority昇順でソートしたポインタ列を作る (順序の決定論性確保)
        std::vector<SystemBase*> sorted;
        sorted.reserve(systems.size());
        for (const auto& s : systems) sorted.push_back(s.get());

        std::stable_sort(sorted.begin(), sorted.end(),
            [](const SystemBase* a, const SystemBase* b) {
                return a->GetPriority() < b->GetPriority();
            });

        // 各SystemをGreedyに既存バッチへ詰める
        for (SystemBase* sys : sorted)
        {
            bool placed = false;
            for (auto& batch : _executionBatches) {
                // batch内に衝突がなければ詰める
                bool conflict = false;
                for (SystemBase* other : batch) {
                    if (IsConflict(sys, other)) { conflict = true; break; }
                }
                if (!conflict) {
                    batch.push_back(sys);
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                _executionBatches.push_back({sys});
            }
        }
    }


    // =========================================================================
    //  Execute
    // =========================================================================

    void TaskScheduler::Execute(float dt, Core::JobSystem* jobSystem)
    {
        for (auto& batch : _executionBatches)
        {
            if (!jobSystem || batch.size() <= 1) {
                // 並列不要 (or 単独) なら直接実行
                for (SystemBase* sys : batch) {
                    sys->UpdateImpl(dt);
                }
                continue;
            }

            // バッチ内のSystemを並列実行
            Core::JobCounter counter;
            for (SystemBase* sys : batch) {
                jobSystem->Execute(
                    [sys, dt]() { sys->UpdateImpl(dt); },
                    &counter);
            }
            jobSystem->WaitForCounter(&counter);
        }
    }

} // namespace CCL::ECS
