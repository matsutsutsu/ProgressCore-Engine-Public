#pragma once
#include <algorithm>
#include <memory>
#include <vector>
#include <string>
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
             return _executionBatches;
         }

        // ------------------------------------------------------------
        // BuildGraph
        // ------------------------------------------------------------
        // 目的:
        //   全 System の依存関係を解析し、
        //   「どの System を同じフレーム内で並列実行できるか」を判定して
        //   実行バッチ（フェーズ）を構築する。
        //
        // 背景:
        //   ECS の System は、読み書きするコンポーネントや対象 Chunk が重なると
        //   並列実行できない（データ競合が発生する）。
        //   また、Priority（ステージ）が異なる場合は必ず順序を守る必要がある。
        //   本関数はそれらの制約を満たしつつ、最大限の並列化を行うための
        //   トポロジカルソートの簡易版である。
        //
        // 出力:
        //   _executionBatches :
        //      外側 vector = 実行順序（Batch 0 → Batch 1 → ...）
        //      内側 vector = 同時に並列実行してよい System の集合
        //
        // 並列化ルール:
        //   1. Priority が異なる System は必ず直列化（同じバッチに入れない）
        //   2. Priority が同じ場合のみ、データ競合チェックを行う
        //   3. 読み書きする TypeID が競合する場合は直列化
        //   4. 同じ Chunk を触る場合も直列化
        //   5. 競合する System より「後のバッチ」に配置する
        //
        // ------------------------------------------------------------    
        void BuildGraph(const std::vector<std::unique_ptr<SystemBase>> &systems)
        {
            // 概要：バッチクリアなどの初期化
            _executionBatches.clear();
            if (systems.empty()) return;

            // 各システムが「どのバッチに入るか」を記録する
            std::vector<int> systemBatchIndex(systems.size(), 0);

            // ------------------------------------------------------------
            // Phase 1: 依存関係解析
            // ------------------------------------------------------------
            // 概要：全システムを走査し、自分より前のシステムとデータ競合があるか調べる
            for (size_t i = 0; i < systems.size(); ++i) {

                // 基本的に競合するシステムがなければ
                // 一番最初に入れたいので -1 で初期化
                int maxDependencyBatch = -1; // 自分がどこのバッチに入るべきか

                // システム i（今調べているシステム）が、何を読み書きするかを取得
                auto readsI  = systems[i]->GetReadTypes();
                auto writesI = systems[i]->GetWriteTypes();

                // 概要：自分より前にあるシステム j と「競合」しないか全部チェックする！
                // 説明：今から入れようとしているブロック（i）と
                // 　　　すでに置かれているブロック（j）の間で
                // 　　　同じコンポーネントを触るかチェックする
                for (size_t j = 0; j < i; ++j) {
                    bool conflict = false; // 競合があるかどうかのフラグ


                    // ステージ（優先度）の境界線を「絶対の壁」にする
                    // 優先度（Enumの値）が違うシステム同士は、データに関係なく必ず順番を待つ
                    if (systems[i]->GetPriority() != systems[j]->GetPriority()) {
                        conflict = true;
                    }
                    // --- 優先度（ステージ）が同じ場合のみ、データ競合をチェックして並列化を試みる ---
                    else {
                        auto readsJ  = systems[j]->GetReadTypes();
                        auto writesJ = systems[j]->GetWriteTypes();

                        bool typeConflict = false;
                        for (auto w : writesI) {
                            if (Contains(readsJ, w) || Contains(writesJ, w)) typeConflict = true;
                        }
                        for (auto r : readsI) {
                            if (Contains(writesJ, r)) typeConflict = true;
                        }

                        if (typeConflict) {
                            const auto& chunksI = systems[i]->GetTargetChunks();
                            const auto& chunksJ = systems[j]->GetTargetChunks();

                            for (CCL::ECS::Core::Chunk* cI : chunksI) {
                                for (CCL::ECS::Core::Chunk* cJ : chunksJ) {
                                    if (cI == cJ) {
                                        conflict = true;
                                        break;
                                    }
                                }
                                if (conflict) break;
                            }
                        }
                    }

                    // 競合（またはステージの違い）があった場合、その先輩のバッチ以降に配置する
                    if (conflict) {
                        // systemBatchIndex[j] は、すでに決まっている j のバッチ番号
                        // maxDependencyBatch は、今の i が入るべきバッチ番号の候補
                        maxDependencyBatch = (std::max)(maxDependencyBatch, systemBatchIndex[j]);
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

            // 構築されたバッチ（実行順序）をLoggerに出力する
           /* CCL_LOG_INFO(LogCategory::ECS, "=== TaskScheduler: Execution Graph Built ===");
            for (size_t i = 0; i < _executionBatches.size(); ++i) {
                std::string batchInfo = "Batch " + std::to_string(i) + ": [";
                for (size_t j = 0; j < _executionBatches[i].size(); ++j) {
                    batchInfo += _executionBatches[i][j]->GetName();
                    if (j < _executionBatches[i].size() - 1) batchInfo += ", ";
                }
                batchInfo += "]";
                CCL_LOG_INFO(LogCategory::ECS, "%s", batchInfo.c_str());
            }
            CCL_LOG_INFO(LogCategory::ECS, "============================================");*/
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