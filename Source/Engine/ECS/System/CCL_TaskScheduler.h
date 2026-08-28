#pragma once
#include <algorithm>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include "Engine/ECS/Core/CCL_Chunk.h"
#include "Engine/ECS/System/CCL_SystemBase.h"
#include "Engine/ECS/Jobs/CCL_JobSystem.h"
#include "Engine/Platform/Core/Logger.h"

namespace CCL::ECS {
    // システムの依存関係を解析し、並列実行可能なバッチを構築するクラス
    class TaskScheduler {
      public:
         //  外部（ImGui）からバッチ構成を読み取るためのAPI
         const std::vector<std::vector<SystemBase*>>& GetBatches() const {
             return m_executionBatches;
         }

        // ------------------------------------------------------------
        // BuildGraph
        // ------------------------------------------------------------
        // 目的:
        //   全 System の依存関係を解析し、実行バッチ（フェーズ）を構築する。
        //
        // 順序の決まり方（強い順に3段）:
        //   1. Priority（ステージ）が違えば必ず直列。ステージが順序の骨格。
        //   2. 同一ステージ内は GetRunAfter / GetRunBefore の宣言をトポロジカル
        //      ソートして確定する。ここが「どちらが先か」を決める唯一の場所。
        //   3. 宣言も型競合も無ければ、並列実行してよいものとして同じバッチに入れる。
        //
        // なぜ3段目だけでは足りなかったか:
        //   Read/Write 型の競合解析は「同時に走らせてよいか」しか判定できない。
        //   競合を見つければ直列化はするが、どちらを先にするかは配列の並び順
        //   まかせになる。その並び順が静的初期化順序に依存していたため、
        //   BoneAttachment と JoltPush の順序が環境によって逆転していた。
        //   2段目の明示宣言はその穴を塞ぐためにある。
        //
        // 出力:
        //   m_executionBatches :
        //      外側 vector = 実行順序（Batch 0 -> Batch 1 -> ...）
        //      内側 vector = 同時に並列実行してよい System の集合
        // ------------------------------------------------------------
        void BuildGraph(const std::vector<std::unique_ptr<SystemBase>> &systems)
        {
            m_executionBatches.clear();
            m_ambiguities.clear();
            if (systems.empty()) return;

            const size_t n = systems.size();

            // ------------------------------------------------------------
            // Phase 0: 明示的な順序宣言を解決し、実行順の添字列を作る
            // ------------------------------------------------------------
            // systems は既に Priority 昇順（同値は System 名順）に並んでいる。
            // その並びを土台に、同一ステージ内だけを宣言に従って組み替える。
            std::vector<size_t> order = ResolveExplicitOrder(systems);

            // ------------------------------------------------------------
            // Phase 1: 依存関係解析
            // ------------------------------------------------------------
            std::vector<int> batchIndexOf(n, 0);

            for (size_t oi = 0; oi < order.size(); ++oi) {
                const size_t i = order[oi];

                int  maxDependencyBatch = -1; // 自分がどこのバッチに入るべきか
                auto readsI             = systems[i]->GetReadTypes();
                auto writesI            = systems[i]->GetWriteTypes();

                // 自分より前に置かれたシステム j と競合しないかを全部見る
                for (size_t oj = 0; oj < oi; ++oj) {
                    const size_t j        = order[oj];
                    bool         conflict = false;

                    // ステージ（優先度）の境界線は絶対の壁。
                    // 優先度が違うシステム同士はデータに関係なく必ず順番を待つ。
                    if (systems[i]->GetPriority() != systems[j]->GetPriority()) {
                        conflict = true;
                    }
                    else if (HasExplicitOrder(*systems[i], *systems[j])) {
                        // 明示的な順序宣言があれば、型が無関係でも直列化する。
                        // Phase 0 で j が先に来るよう並べ替え済みなので、
                        // ここで直列化すれば宣言通りの順序になる。
                        conflict = true;
                    }
                    else {
                        // --- 型競合のチェック（並列化してよいかの判定） ---
                        auto readsJ  = systems[j]->GetReadTypes();
                        auto writesJ = systems[j]->GetWriteTypes();

                        bool typeConflict = false;
                        for (auto w : writesI) {
                            if (Contains(readsJ, w) || Contains(writesJ, w)) {
                                typeConflict = true;
                                break;
                            }
                        }
                        if (!typeConflict) {
                            for (auto r : readsI) {
                                if (Contains(writesJ, r)) {
                                    typeConflict = true;
                                    break;
                                }
                            }
                        }

                        // Chunk単位の競合チェックは初期化時点の情報しか持てず、
                        // 実行中に Chunk が変わると誤判定するため無効のまま。

                        if (typeConflict) {
                            conflict = true;

                            // 型は競合するのに順序が宣言されていない ＝ 順序不定。
                            // 直列化はされるが、どちらが先かは並び順まかせになる。
                            // 潜在バグとして記録し、起動時にまとめて報告する。
                            m_ambiguities.push_back({systems[j]->GetName(),
                                systems[i]->GetName(),
                                systems[i]->GetPriority()});
                        }
                    }

                    if (conflict) {
                        maxDependencyBatch = (std::max)(maxDependencyBatch, batchIndexOf[j]);
                    }
                }

                batchIndexOf[i] = maxDependencyBatch + 1;

                if (static_cast<size_t>(batchIndexOf[i]) >= m_executionBatches.size()) {
                    m_executionBatches.resize(batchIndexOf[i] + 1);
                }
                m_executionBatches[batchIndexOf[i]].push_back(systems[i].get());
            }
        }

        // ------------------------------------------------------------
        // 順序が不定なペア（同一ステージ・型競合あり・順序宣言なし）
        // ------------------------------------------------------------
        struct Ambiguity {
            std::string first;    ///< 今の並びで先に走る方
            std::string second;   ///< 今の並びで後に走る方
            int         priority; ///< 両者が属するステージ値
        };

        const std::vector<Ambiguity> &GetAmbiguities() const { return m_ambiguities; }

        // ------------------------------------------------------------
        // 検出した順序不定ペアを Logger へ出力する
        // ------------------------------------------------------------
        // 「直列化はされているが、どちらが先かはソートの並び順で決まっている」組を
        // 全部出す。ここに出たペアに実際の依存関係があるなら、GetRunAfter で順序を
        // 宣言するか、ステージを分けること。逆に本当に順不同でよいなら放置してよい。
        void LogAmbiguities(const char *groupLabel) const
        {
            if (m_ambiguities.empty()) {
                CCL_LOG_SUCCESS(LogCategory::ECS,
                    "[Scheduler/%s] 順序が不定なシステムペアはありません", groupLabel);
                return;
            }

            CCL_LOG_WARN(LogCategory::ECS,
                "[Scheduler/%s] 順序が不定なシステムペア: %zu 組"
                "（型は競合するのに前後が宣言されていない。実依存があれば GetRunAfter を書くこと）",
                groupLabel, m_ambiguities.size());

            for (const Ambiguity &a : m_ambiguities) {
                CCL_LOG_WARN(LogCategory::ECS,
                    "    [stage %4d] %s -> %s  (今はこの順。保証はされていない)",
                    a.priority, a.first.c_str(), a.second.c_str());
            }
        }

        // 構築されたバッチ順に実行する
        void Execute(float dt, JobSystem *jobSystem)
        {
            // バッチ（シフト表のまとまり）ごとに順番に見ていく
            for (auto &batch : m_executionBatches) {
                if (batch.empty()) continue;

                 // ① バッチが1個 or プール未注入なら直列実行
                //    （jobSystem==nullptr の安全弁：プール無しでも必ず動く）
                if (batch.size() == 1 || jobSystem == nullptr) {
                    for (SystemBase* sys : batch) {
                        sys->UpdateImpl(dt);
                    }
                }
                // ② バッチの中にシステムが複数（並列可能）ある場合
                else {
                    // バッチ内に複数システムがある = 完全にデータが独立している = 超並列実行可能！
                    JobCounter counter;

                    // システムの数だけ、JobSystem（厨房）に仕事を投げる
                    for (SystemBase *sys : batch) {
                        // 【バトンの作成】
                        // [sys, dt] をキャプチャ（メモリ上にコピー）し、
                        // 「sys->UpdateImpl(dt) を実行する」という関数ポインタ（ラムダ式）を作る。
                        auto jobTask = [sys, dt]() { sys->UpdateImpl(dt); };

                        // 【バトンの投下】
                        // そのラムダ式と、カウンターのポインタを JobSystem に渡す。
                        jobSystem->Execute(jobTask, &counter);
                    }
                    // このバッチの全システムが終わるまで待つ
                    jobSystem->WaitForCounter(&counter);
                }
            }
        }

      private:
        // あるTypeIDのリストに、特定のTypeIDが含まれているか
        bool Contains(const std::vector<TypeID> &list, TypeID val) const
        {
            return std::find(list.begin(), list.end(), val) != list.end();
        }

        // a と b の間に明示的な順序宣言があるか（どちら向きでも true）
        static bool HasExplicitOrder(const SystemBase &a, const SystemBase &b)
        {
            for (const char *nm : a.GetRunAfter())  if (nm && b.GetName() == nm) return true;
            for (const char *nm : a.GetRunBefore()) if (nm && b.GetName() == nm) return true;
            for (const char *nm : b.GetRunAfter())  if (nm && a.GetName() == nm) return true;
            for (const char *nm : b.GetRunBefore()) if (nm && a.GetName() == nm) return true;
            return false;
        }

        // ------------------------------------------------------------
        // ResolveExplicitOrder
        // ------------------------------------------------------------
        // systems は Priority 昇順（同値は System 名順）に並んでいる前提。
        // 同一 Priority の区間ごとに GetRunAfter / GetRunBefore を辺とみなして
        // トポロジカルソート（Kahn法）し、実行順の添字列を返す。
        //
        // 決定性のため、同時に実行可能になった候補は常に元の並び（＝名前順）で
        // 取り出す。宣言が1つも無ければ結果は入力と完全に同じ並びになる。
        std::vector<size_t> ResolveExplicitOrder(
            const std::vector<std::unique_ptr<SystemBase>> &systems) const
        {
            const size_t        n = systems.size();
            std::vector<size_t> result;
            result.reserve(n);

            // 名前 -> 添字。宣言の検証に使うので全システムを対象にする。
            std::unordered_map<std::string, size_t> indexByName;
            for (size_t i = 0; i < n; ++i) indexByName[systems[i]->GetName()] = i;

            size_t bandStart = 0;
            while (bandStart < n) {
                // 同一 Priority の区間 [bandStart, bandEnd) を切り出す
                size_t    bandEnd = bandStart;
                const int prio    = systems[bandStart]->GetPriority();
                while (bandEnd < n && systems[bandEnd]->GetPriority() == prio) ++bandEnd;

                const size_t count = bandEnd - bandStart;
                if (count == 1) {
                    result.push_back(bandStart);
                    bandStart = bandEnd;
                    continue;
                }

                // 区間内の局所添字で隣接リストを組む
                std::vector<std::vector<size_t>> next(count);
                std::vector<int>                 indeg(count, 0);

                // from -> to（from が先に走る）の辺を張る
                auto addEdge = [&](size_t from, size_t to) {
                    next[from].push_back(to);
                    ++indeg[to];
                };

                // 宣言名を同一区間内の局所添字へ解決する。
                // 解決できない場合は黙って無視せず、必ず理由をログに出す。
                auto resolveLocal = [&](const SystemBase &owner, const char *name,
                                        size_t &outLocal) -> bool {
                    if (name == nullptr) return false;

                    auto it = indexByName.find(name);
                    if (it == indexByName.end()) {
                        CCL_LOG_ERROR(LogCategory::ECS,
                            "[Scheduler] %s の順序宣言 \"%s\" に該当するシステムがありません"
                            "（綴り間違い、または未登録）",
                            owner.GetName().c_str(), name);
                        return false;
                    }

                    const size_t g = it->second;
                    if (systems[g]->GetPriority() != prio) {
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

                for (size_t k = 0; k < count; ++k) {
                    const SystemBase &sys   = *systems[bandStart + k];
                    size_t            other = 0;

                    // GetRunAfter : other が先、自分が後
                    for (const char *nm : sys.GetRunAfter())
                        if (resolveLocal(sys, nm, other)) addEdge(other, k);

                    // GetRunBefore : 自分が先、other が後
                    for (const char *nm : sys.GetRunBefore())
                        if (resolveLocal(sys, nm, other)) addEdge(k, other);
                }

                // Kahn法。候補は常に局所添字の小さい順（＝名前順）で取るので結果は決定的。
                std::vector<size_t> sorted;
                sorted.reserve(count);
                std::vector<bool> done(count, false);

                for (size_t step = 0; step < count; ++step) {
                    size_t pick = count;
                    for (size_t k = 0; k < count; ++k) {
                        if (!done[k] && indeg[k] == 0) { pick = k; break; }
                    }

                    if (pick == count) {
                        // 入次数0が無い ＝ 宣言が循環している。
                        // 順序を決められないので、残りは名前順のまま流して続行する。
                        CCL_LOG_ERROR(LogCategory::ECS,
                            "[Scheduler] stage %d の順序宣言に循環があります。"
                            "残り %zu システムは名前順で実行します",
                            prio, count - step);
                        for (size_t k = 0; k < count; ++k) {
                            if (!done[k]) { sorted.push_back(k); done[k] = true; }
                        }
                        break;
                    }

                    done[pick] = true;
                    sorted.push_back(pick);
                    for (size_t to : next[pick]) --indeg[to];
                }

                for (size_t local : sorted) result.push_back(bandStart + local);
                bandStart = bandEnd;
            }

            return result;
        }


        // 外側のvector: 順番に実行されるフェーズ（バッチ）
        // 内側のvector: そのフェーズ内で「同時に並列実行してよい」システムのリスト
        std::vector<std::vector<SystemBase *>> m_executionBatches;

        // BuildGraph 中に検出した「順序が不定なペア」
        std::vector<Ambiguity> m_ambiguities;
    };
} // namespace CCL::ECS