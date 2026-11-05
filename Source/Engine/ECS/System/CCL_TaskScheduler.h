#pragma once
// ===========================================================================
// 概要: System 間の依存関係を解析し、並列実行できるバッチ列を組んで実行するクラス
//
// 責務:
//   - ステージ・明示的な順序宣言・Read/Write 型競合から実行バッチを構築する
//   - 型は競合するのに順序が宣言されていないペア（順序不定）の検出と報告
//   - バッチを順に、バッチ内は JobSystem で並列に実行する
// ===========================================================================
#include <algorithm>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include "Engine/ECS/Core/CCL_Chunk.h"
#include "Engine/ECS/System/CCL_SystemBase.h"
#include "Engine/ECS/Jobs/CCL_JobSystem.h"
#include "Engine/Platform/Core/Logger.h"

namespace CCL::ECS
{
    class TaskScheduler
    {
    public:
        // エディタ（ImGui）がバッチ構成を表示するために読む
        const std::vector<std::vector<SystemBase *>> &GetBatches() const
        {
            return m_executionBatches;
        }

        // 全 System の依存を解析し、実行のバッチを m_executionBatches に作る（systems はステージの昇順、同じなら名前順）
        // ステージが違えば直列。同じステージの中は GetRunAfter / GetRunBefore の宣言で順を決め、
        // 宣言も型の競合もないものは同じバッチで並列に走らせる
        void BuildGraph(const std::vector<std::unique_ptr<SystemBase>> &systems)
        {
            m_executionBatches.clear();
            m_ambiguities.clear();
            if (systems.empty()) return;

            const size_t n = systems.size();

            // Phase 0: 明示的な順序宣言を解決し、実行順の添字列を作る
            // systems の並びを土台に、同一ステージ内だけを宣言に従って組み替える
            std::vector<size_t> order = ResolveExplicitOrder(systems);

            // Phase 1: 前に置かれた System との競合を調べ、入るバッチを決める
            std::vector<int> batchIndexOf(n, 0);

            for (size_t oi = 0; oi < order.size(); ++oi)
            {
                const size_t i = order[oi];

                int  maxDependencyBatch = -1; // 待つ必要がある相手のうち、最も後ろのバッチ番号
                auto readsI             = systems[i]->GetReadTypes();
                auto writesI            = systems[i]->GetWriteTypes();

                for (size_t oj = 0; oj < oi; ++oj)
                {
                    const size_t j        = order[oj];
                    bool         conflict = false;

                    // ステージ（優先度）の境界は絶対の壁
                    // 優先度が違う System 同士はデータに関係なく必ず順番を待つ
                    if (systems[i]->GetPriority() != systems[j]->GetPriority())
                    {
                        conflict = true;
                    }
                    else if (HasExplicitOrder(*systems[i], *systems[j]))
                    {
                        // 明示的な順序宣言があれば、型が無関係でも直列化する
                        // Phase 0 で j が先に来るよう並べ替え済みなので、
                        // ここで直列化すれば宣言通りの順序になる
                        conflict = true;
                    }
                    else
                    {
                        // 型競合のチェック（並列化してよいかの判定）
                        auto readsJ  = systems[j]->GetReadTypes();
                        auto writesJ = systems[j]->GetWriteTypes();

                        bool typeConflict = false;
                        for (auto w : writesI)
                        {
                            if (Contains(readsJ, w) || Contains(writesJ, w))
                            {
                                typeConflict = true;
                                break;
                            }
                        }
                        if (!typeConflict)
                        {
                            for (auto r : readsI)
                            {
                                if (Contains(writesJ, r))
                                {
                                    typeConflict = true;
                                    break;
                                }
                            }
                        }

                        // Chunk 単位の競合チェックは初期化時点の情報しか持てず、
                        // 実行中に Chunk が変わると誤判定するため無効のまま

                        if (typeConflict)
                        {
                            conflict = true;

                            // 型は競合するのに順序が宣言されていない ＝ 順序不定
                            // 直列化はされるが、どちらが先かは並び順まかせになる
                            // 潜在バグとして記録し、起動時にまとめて報告する
                            m_ambiguities.push_back({systems[j]->GetName(),
                                                     systems[i]->GetName(),
                                                     systems[i]->GetPriority()});
                        }
                    }

                    if (conflict)
                    {
                        maxDependencyBatch = (std::max)(maxDependencyBatch, batchIndexOf[j]);
                    }
                }

                batchIndexOf[i] = maxDependencyBatch + 1;

                if (static_cast<size_t>(batchIndexOf[i]) >= m_executionBatches.size())
                {
                    m_executionBatches.resize(batchIndexOf[i] + 1);
                }
                m_executionBatches[batchIndexOf[i]].push_back(systems[i].get());
            }
        }

        // 順序が不定なペア（同一ステージ・型競合あり・順序宣言なし）
        struct Ambiguity
        {
            std::string first;    // 今の並びで先に走る方
            std::string second;   // 今の並びで後に走る方
            int         priority; // 両者が属するステージ値
        };

        const std::vector<Ambiguity> &GetAmbiguities() const { return m_ambiguities; }

        // 直列にはなっているが、どちらが先かがソートの並びで決まっている組をすべてログに出す
        // 本当に依存があるなら、GetRunAfter で順を宣言するか、ステージを分ける
        void LogAmbiguities(const char *groupLabel) const
        {
            if (m_ambiguities.empty())
            {
                CCL_LOG_SUCCESS(LogCategory::ECS,
                                "[Scheduler/%s] 順序が不定なシステムペアはありません", groupLabel);
                return;
            }

            CCL_LOG_WARN(LogCategory::ECS,
                         "[Scheduler/%s] 順序が不定なシステムペア: %zu 組"
                         "（型は競合するのに前後が宣言されていない。実依存があれば GetRunAfter を書くこと）",
                         groupLabel, m_ambiguities.size());

            for (const Ambiguity &a : m_ambiguities)
            {
                CCL_LOG_WARN(LogCategory::ECS,
                             "    [stage %4d] %s -> %s  (今はこの順。保証はされていない)",
                             a.priority, a.first.c_str(), a.second.c_str());
            }
        }

        // 構築したバッチを順に実行する。バッチ内の System は並列に走らせる
        void Execute(float dt, JobSystem *jobSystem)
        {
            for (auto &batch : m_executionBatches)
            {
                if (batch.empty()) continue;

                // バッチ内が1個なら並列化の意味がない
                // プール未注入（nullptr）でも必ず動くよう、ここで直列に逃がす
                if (batch.size() == 1 || jobSystem == nullptr)
                {
                    for (SystemBase *sys : batch)
                    {
                        sys->UpdateImpl(dt);
                    }
                }
                else
                {
                    // 同じバッチの System 同士はデータが競合しないことが保証済み
                    JobCounter counter;

                    for (SystemBase *sys : batch)
                    {
                        // ジョブはループを抜けた後に別スレッドで走るので、
                        // sys と dt は参照ではなく値でキャプチャしておく
                        auto jobTask = [sys, dt]() { sys->UpdateImpl(dt); };

                        jobSystem->Execute(jobTask, &counter);
                    }
                    // 次のバッチはこのバッチの結果に依存するので、全員の完了を待つ
                    jobSystem->WaitForCounter(&counter);
                }
            }
        }

    private:
        bool Contains(const std::vector<TypeID> &list, TypeID val) const
        {
            return std::find(list.begin(), list.end(), val) != list.end();
        }

        // a と b の間に明示的な順序宣言があるか（どちら向きでも true）
        static bool HasExplicitOrder(const SystemBase &a, const SystemBase &b)
        {
            for (const char *nm : a.GetRunAfter())
                if (nm && b.GetName() == nm) return true;
            for (const char *nm : a.GetRunBefore())
                if (nm && b.GetName() == nm) return true;
            for (const char *nm : b.GetRunAfter())
                if (nm && a.GetName() == nm) return true;
            for (const char *nm : b.GetRunBefore())
                if (nm && a.GetName() == nm) return true;
            return false;
        }

        // 同じステージの区間ごとに、GetRunAfter / GetRunBefore を辺として Kahn 法で並べ、実行順の添字を返す
        // 同時に並べられる候補は元の並び（名前順）で取り出すので、宣言がなければ入力と同じ並びになる
        std::vector<size_t> ResolveExplicitOrder(
            const std::vector<std::unique_ptr<SystemBase>> &systems) const
        {
            const size_t        n = systems.size();
            std::vector<size_t> result;
            result.reserve(n);

            // 名前 -> 添字。別ステージを指す宣言も検出したいので全 System を対象にする
            std::unordered_map<std::string, size_t> indexByName;
            for (size_t i = 0; i < n; ++i)
                indexByName[systems[i]->GetName()] = i;

            size_t bandStart = 0;
            while (bandStart < n)
            {
                // 同一 Priority の区間 [bandStart, bandEnd) を切り出す
                size_t    bandEnd = bandStart;
                const int prio    = systems[bandStart]->GetPriority();
                while (bandEnd < n && systems[bandEnd]->GetPriority() == prio)
                    ++bandEnd;

                const size_t count = bandEnd - bandStart;
                if (count == 1)
                {
                    result.push_back(bandStart);
                    bandStart = bandEnd;
                    continue;
                }

                // 区間内の局所添字で隣接リストと入次数を組む
                std::vector<std::vector<size_t>> next(count);
                std::vector<int>                 indeg(count, 0);

                // from -> to（from が先に走る）の辺を張る
                auto addEdge = [&](size_t from, size_t to) {
                    next[from].push_back(to);
                    ++indeg[to];
                };

                // 宣言名を同一区間内の局所添字へ解決する
                // 解決できない場合は黙って無視せず、必ず理由をログに出す
                auto resolveLocal = [&](const SystemBase &owner, const char *name,
                                        size_t &outLocal) -> bool {
                    if (name == nullptr) return false;

                    auto it = indexByName.find(name);
                    if (it == indexByName.end())
                    {
                        CCL_LOG_ERROR(LogCategory::ECS,
                                      "[Scheduler] %s の順序宣言 \"%s\" に該当するシステムがありません"
                                      "（綴り間違い、または未登録）",
                                      owner.GetName().c_str(), name);
                        return false;
                    }

                    const size_t g = it->second;
                    if (systems[g]->GetPriority() != prio)
                    {
                        CCL_LOG_WARN(LogCategory::ECS,
                                     "[Scheduler] %s (stage %d) の順序宣言 \"%s\" は別ステージ (stage %d) を"
                                     "指しています。ステージが違えば順序は元から保証されるため不要です",
                                     owner.GetName().c_str(), prio, name, systems[g]->GetPriority());
                        return false;
                    }

                    if (g < bandStart || g >= bandEnd) return false;
                    outLocal = g - bandStart;
                    return true;
                };

                for (size_t k = 0; k < count; ++k)
                {
                    const SystemBase &sys   = *systems[bandStart + k];
                    size_t            other = 0;

                    // GetRunAfter : other が先、自分が後
                    for (const char *nm : sys.GetRunAfter())
                        if (resolveLocal(sys, nm, other)) addEdge(other, k);

                    // GetRunBefore : 自分が先、other が後
                    for (const char *nm : sys.GetRunBefore())
                        if (resolveLocal(sys, nm, other)) addEdge(k, other);
                }

                // Kahn 法。候補は常に局所添字の小さい順（＝名前順）で取るので結果は決定的
                std::vector<size_t> sorted;
                sorted.reserve(count);
                std::vector<bool> done(count, false);

                for (size_t step = 0; step < count; ++step)
                {
                    size_t pick = count;
                    for (size_t k = 0; k < count; ++k)
                    {
                        if (!done[k] && indeg[k] == 0)
                        {
                            pick = k;
                            break;
                        }
                    }

                    if (pick == count)
                    {
                        // 入次数0がない ＝ 宣言が循環している
                        // 順序を決められないので、残りは名前順のまま流して続行する
                        CCL_LOG_ERROR(LogCategory::ECS,
                                      "[Scheduler] stage %d の順序宣言に循環があります。"
                                      "残り %zu システムは名前順で実行します",
                                      prio, count - step);
                        for (size_t k = 0; k < count; ++k)
                        {
                            if (!done[k])
                            {
                                sorted.push_back(k);
                                done[k] = true;
                            }
                        }
                        break;
                    }

                    done[pick] = true;
                    sorted.push_back(pick);
                    for (size_t to : next[pick])
                        --indeg[to];
                }

                for (size_t local : sorted)
                    result.push_back(bandStart + local);
                bandStart = bandEnd;
            }

            return result;
        }

        // 外側: 順番に実行するバッチ／内側: そのバッチ内で同時に並列実行してよい System
        std::vector<std::vector<SystemBase *>> m_executionBatches;

        std::vector<Ambiguity> m_ambiguities; // BuildGraph 中に検出した順序不定ペア
    };
} // namespace CCL::ECS
