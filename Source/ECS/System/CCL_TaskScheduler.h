#pragma once
#include "CCL_System.h"
#include <algorithm>
#include <memory>
#include <vector>
#include "ECS/Core/CCL_Chunk.h"

namespace CCL::ECS {
    // システムの依存関係を解析し、並列実行可能なバッチを構築するクラス
    class TaskScheduler {
      public:
        // システムのリストを解析して実行計画（グラフ）を作る
        void BuildGraph(const std::vector<std::unique_ptr<SystemBase>> &systems)
        {
            _executionBatches.clear();
            if (systems.empty()) return;

            // 各システムが「どのバッチに入るか」を記録する
            std::vector<int> systemBatchIndex(systems.size(), 0);

            // 依存関係（トポロジカルソートの簡易版）
            // 全システムを走査し、自分より前のシステムとデータ競合があるか調べる
            for (size_t i = 0; i < systems.size(); ++i) {
                int maxDependencyBatch = -1; // 自分がどこのバッチに入るべきか

                // システム i（今調べているシステム）が、何を読み書きするかを取得
                auto readsI  = systems[i]->GetReadTypes();
                auto writesI = systems[i]->GetWriteTypes();

                // 自分より前にあるシステム j と「競合」しないか全部チェックする！
                for (size_t j = 0; j < i; ++j) {
                    auto readsJ  = systems[j]->GetReadTypes();
                    auto writesJ = systems[j]->GetWriteTypes();


                    bool typeConflict = false;

                    // 競合条件1：自分が Write（書き換える）データが、
                    // 相手の Read か Write に含まれていたらアウト！  Chunkレベルの厳密な判定
                    for (auto w : writesI) {
                        if (Contains(readsJ, w) || Contains(writesJ, w)) typeConflict = true;
                    }

                    // 競合条件2：自分が Read（読む）データが、
                    // 相手の Write に含まれていたらアウト！（読んでいる途中で書き換えられたら困る）
                    for (auto r : readsI) {
                        if (Contains(writesJ, r)) typeConflict = true;
                    }

                    bool conflict = false;

                    // 型の競合がある場合のみ、さらに「対象のChunkが被っているか」を調べる
                    if (typeConflict) {
                        const auto& chunksI = systems[i]->GetTargetChunks();
                        const auto& chunksJ = systems[j]->GetTargetChunks();

                        for (CCL::ECS::Core::Chunk* cI : chunksI) {
                            for (CCL::ECS::Core::Chunk* cJ : chunksJ) {
                                // 同じチャンクのポインタを両方が持っていたら、本物の競合（データレース）！
                                if (cI == cJ) {
                                    conflict = true;
                                    break;
                                }
                            }
                            if (conflict) break;
                        }
                    }
                }

                // 依存するシステムの次のバッチに配置する
                systemBatchIndex[i] = maxDependencyBatch + 1;

                // 自分のバッチ番号は、「競合した相手のバッチ番号 + 1 (つまり次のターン)」にする！
                // 誰とも競合しなかったら、-1 + 1 = 0 で、最初のターンに組み込まれる。
                if (systemBatchIndex[i] >= _executionBatches.size()) {
                    _executionBatches.resize(systemBatchIndex[i] + 1);
                }

                // バッチにシステムを登録
                _executionBatches[systemBatchIndex[i]].push_back(systems[i].get());
            }
        }

        // 構築されたバッチ順に実行する
        void Execute(float dt, Core::JobSystem *jobSystem)
        {
            // バッチ（シフト表のまとまり）ごとに順番に見ていく
            for (auto &batch : _executionBatches) {
                if (batch.empty()) continue;

                 // ① バッチの中にシステムが1個しかない場合
                if (batch.size() == 1) {   
                    // バッチ内に1つしかないなら直列実行
                    batch[0]->UpdateImpl(dt);
                }
                // ② バッチの中にシステムが複数（並列可能）ある場合
                else {      
                    // バッチ内に複数システムがある = 完全にデータが独立している = 超並列実行可能！
                    Core::JobCounter counter;

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

        // 外側のvector: 順番に実行されるフェーズ（バッチ）
        // 内側のvector: そのフェーズ内で「同時に並列実行してよい」システムのリスト
        std::vector<std::vector<SystemBase *>> _executionBatches;
    };
} // namespace CCL::ECS