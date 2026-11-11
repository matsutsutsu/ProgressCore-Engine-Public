#pragma once
// ===========================================================================
// 概要: 必要な Component の構成（Filter）を型から作り、型安全な ForEach を提供する System の基底クラス
//
// 責務:
//   - Read<T> / Write<T> から Filter を作り、Read<T> は const T& で渡して書き込みを型で禁止する
//   - ForEach / ForEachParallel / ForEachWithID / ForEachWithIDParallel を提供する
//   - イベントの購読を、System の破棄時に自動で解除する（ListenEvent）
//   - Read / Write の型を、TaskScheduler の依存解析へ自動で申告する
//
// 備考: SystemBase は処理する Chunk を受け取る器だけで、重いテンプレートの展開はこのヘッダに集める
//
// @code
//   class MovementSystem : public IfSystem<
//       MovementSystem,
//       Write<Position>,
//       Read<Velocity>>
//   {
//   public:
//       MovementSystem() : IfSystem("MovementSystem") {}
//
//       void Update(float dt) override
//       {
//           ForEachParallel([dt](Position &p, const Velocity &v) { p.x += v.x * dt; });
//       }
//   };
// @endcode
// ===========================================================================

#include <functional>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include "CCL_SystemBase.h"
#include "CCL_SystemAccess.h"

// ForEach の実装で Chunk / World / JobSystem の完全定義が必要
#include "Engine/ECS/Common/CCL_Types.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Jobs/CCL_JobSystem.h"

#include "tracy/Tracy.hpp"

namespace CCL::ECS
{
    // Derived は CRTP の派生クラス自身、AccessTypes は Read<T> / Write<T> の並び
    // 例: IfSystem<MovementSystem, Write<Position>, Read<Velocity>> なら、Filter は {Position, Velocity}
    template <class Derived, class... AccessTypes>
    class IfSystem : public SystemBase
    {
    public:
        // AccessTypes から Filter（Archetype）を作る。name はデバッグと GUI の表示に使う
        IfSystem(const std::string &name) : SystemBase(name)
        {
            // Read<T>/Write<T> のラッパーから生の型 RawType を取り出して Filter を生成
            m_filter = ArchetypeHelper::Generate<typename AccessTypes::RawType...>();
        }

        // 登録済みのイベント購読をすべて自動解除する
        virtual ~IfSystem() override
        {
            for (auto &cleanup : m_eventCleanups)
            {
                cleanup();
            }
            m_eventCleanups.clear();
        }

    private:
        // ListenEvent で登録した購読解除の処理。std::function に包み、イベントの型によらず同じ型で並べる
        std::vector<std::function<void()>> m_eventCleanups;

    protected:
        // ============================================================
        // イベント購読 API
        // ============================================================

        // イベントを購読する。解除の処理を m_eventCleanups に積み、デストラクタで一括して解除する
        // EventBus の Subscribe / Unsubscribe を直接使うと、解除し忘れる
        template <typename EventType>
        void ListenEvent(std::function<void(const EventType &)> callback)
        {
            if (!m_world) return;

            // 1. EventBus に登録し、チケットIDを受け取る
            auto ticketID = m_world->GetEventBus().Subscribe<EventType>(callback);

            // 2. 「このイベントを解除する」処理そのものをラムダにして保管する
            //    （EventType が何だったかをラムダ内に焼き付けて保存）
            m_eventCleanups.push_back([this, ticketID]() {
                if (this->m_world)
                {
                    this->m_world->GetEventBus().Unsubscribe<EventType>(ticketID);
                }
            });
        }

        // 指定した型を1つでも持つ Entity を、この System の処理対象から外す。コンストラクタで呼ぶ
        // 例: SetExclude<DeadTag, Frozen>();
        template <typename... ExcludeTypes>
        void SetExclude()
        {
            m_excludeFilter = ArchetypeHelper::Generate<ExcludeTypes...>();
        }

        // ============================================================
        // ForEach（シングルスレッド）
        // ============================================================

        // TargetChunk の全 Entity に func を呼ぶ。Read<T> は const T&、Write<T> は T& で渡す
        // 例: ForEach([dt](Position &p, const Velocity &v) { p.x += v.x * dt; });
        template <typename Func>
        void ForEach(Func &&func)
        {
            for (Chunk *chunk : m_targetChunks)
            {
                const size_t count = chunk->GetEntityCount();
                if (count == 0) continue;

                // 1. 各コンポーネントの配列の先頭を、ループの外で1回だけ取り出して tuple にまとめる
                //    AccessTypes::RawType... を展開するので、tuple<TransformComponent*, VelocityComponent*, ...> になる
                auto componentArrays = std::make_tuple(
                    static_cast<typename AccessTypes::RawType *>(
                        chunk->GetComponentPtrByType(
                            TypeInfo<typename AccessTypes::RawType>::ID(), 0))...);

                const bool hasDestroyed = chunk->HasDestroyedEntities();

                // 2. Chunk 内の Entity をループ
                for (size_t i = 0; i < count; ++i)
                {
                    // 削除予約済み Entity をスキップ
                    // hasDestroyed が false（大半）なら右辺の重い関数呼び出しを
                    // 短絡評価で省略し、通常時の速度を保つ
                    if (hasDestroyed && chunk->IsEntityDestroyed(i)) continue;

                    // index_sequence で生成した整数列を使って tuple から
                    // 各コンポーネント配列を取り出し、func に展開して渡す
                    ForEachImpl(func,
                                componentArrays,
                                i,
                                std::make_index_sequence<sizeof...(AccessTypes)>{});
                }
            }
        }

        // ============================================================
        // ForEachParallel（並列）
        // ============================================================

        // ForEach を、Chunk をワーカーに分けて並列に回す。同じ Chunk を2つのジョブが触ることはない
        // Chunk が少ない・JobSystem がない・enableMultiThread が false のときは、1スレッドで回す
        template <typename Func>
        void ForEachParallel(Func &&func)
        {
            if (!m_jobSystem || !this->enableMultiThread)
            {
                ForEach(func);
                return;
            }

            const size_t totalChunks = m_targetChunks.size();
            if (totalChunks == 0) return;

            unsigned int threadCount = std::thread::hardware_concurrency();
            if (threadCount == 0) threadCount = 4;

            // Chunk 数がスレッド数の2倍未満なら、ジョブ発行の準備コストに負けるので直列
            if (totalChunks < threadCount * 2)
            {
                ForEach(func);
                return;
            }

            JobCounter counter;

            // 1ジョブが担当する Chunk 数（均等割り、切り上げ）
            const size_t chunksPerJob = (totalChunks + threadCount - 1) / threadCount;

            // 外側ループ: ジョブ単位で担当範囲 [i, end) を切り出して発行する
            for (size_t i = 0; i < totalChunks; i += chunksPerJob)
            {
                const size_t end = (std::min)(i + chunksPerJob, totalChunks);

                m_jobSystem->Execute(
                    [this, i, end, func]() {
                        // Tracy 上で System 名が見えるよう、ゾーン名を動的に付け直す
                        // " [Batch]" を付けて、メインスレッドの通常 Update と見分けられるようにする
                        ZoneScoped;
                        std::string batchName = m_name + " [Batch]";
                        ZoneName(batchName.c_str(), batchName.size());

                        // 以下はワーカースレッド内。担当範囲の Chunk を順に処理する
                        for (size_t k = i; k < end; ++k)
                        {
                            Chunk       *chunk = m_targetChunks[k];
                            const size_t count = chunk->GetEntityCount();
                            if (count == 0) continue;

                            // 配列ポインタの取得は ForEach と同じ（ループ外で1回だけ）
                            auto componentArrays = std::make_tuple(
                                static_cast<typename AccessTypes::RawType *>(
                                    chunk->GetComponentPtrByType(
                                        TypeInfo<typename AccessTypes::RawType>::ID(), 0))...);

                            const bool hasDestroyed = chunk->HasDestroyedEntities();

                            for (size_t entIdx = 0; entIdx < count; ++entIdx)
                            {
                                if (hasDestroyed && chunk->IsEntityDestroyed(entIdx)) continue;

                                this->ForEachImpl(func,
                                                  componentArrays,
                                                  entIdx,
                                                  std::make_index_sequence<sizeof...(AccessTypes)>{});
                            }
                        }
                    },
                    &counter);
            }

            // 全ジョブの完了を待つ（待っている間はメインスレッドもジョブを手伝う）
            {
                ZoneScopedN("MainThread Wait");

                m_jobSystem->WaitForCounter(&counter);
            }
        }

        // ============================================================
        // ForEachWithID（EntityID 付き、シングルスレッド）
        // ============================================================

        // ForEach に EntityID も渡す版。ほかの Entity を参照するときに使う
        template <typename Func>
        void ForEachWithID(Func &&func)
        {
            for (Chunk *chunk : m_targetChunks)
            {
                const size_t count = chunk->GetEntityCount();
                if (count == 0) continue;

                auto componentArrays = std::make_tuple(
                    static_cast<typename AccessTypes::RawType *>(
                        chunk->GetComponentPtrByType(
                            TypeInfo<typename AccessTypes::RawType>::ID(), 0))...);

                const EntityID *entityIDs    = chunk->GetEntityIDs();
                const bool      hasDestroyed = chunk->HasDestroyedEntities();

                for (size_t i = 0; i < count; ++i)
                {
                    if (hasDestroyed && chunk->IsEntityDestroyed(i)) continue;

                    // Entity ごとの関数呼び出しを避け、先に取った配列から直接読む
                    const EntityID id = entityIDs[i];

                    ForEachWithIDImpl(func,
                                      id,
                                      componentArrays,
                                      i,
                                      std::make_index_sequence<sizeof...(AccessTypes)>{});
                }
            }
        }

        // ============================================================
        // ForEachWithIDParallel（EntityID 付き、並列）
        // ============================================================

        // ForEachWithID の並列実行版。分割とフォールバックの方針は ForEachParallel と同じ
        template <typename Func>
        void ForEachWithIDParallel(Func &&func)
        {
            if (!m_jobSystem || !this->enableMultiThread)
            {
                ForEachWithID(func);
                return;
            }

            const size_t totalChunks = m_targetChunks.size();
            if (totalChunks == 0) return;

            unsigned int threadCount = std::thread::hardware_concurrency();
            if (threadCount == 0) threadCount = 4;

            if (totalChunks < threadCount * 2)
            {
                ForEachWithID(func);
                return;
            }

            JobCounter counter;

            const size_t chunksPerJob = (totalChunks + threadCount - 1) / threadCount;

            for (size_t i = 0; i < totalChunks; i += chunksPerJob)
            {
                const size_t end = (std::min)(i + chunksPerJob, totalChunks);

                m_jobSystem->Execute(
                    [this, i, end, func]() {
                        ZoneScoped;
                        std::string batchName = m_name + " [Batch]";
                        ZoneName(batchName.c_str(), batchName.size());

                        for (size_t k = i; k < end; ++k)
                        {
                            Chunk       *chunk = m_targetChunks[k];
                            const size_t count = chunk->GetEntityCount();
                            if (count == 0) continue;

                            auto componentArrays = std::make_tuple(
                                static_cast<typename AccessTypes::RawType *>(
                                    chunk->GetComponentPtrByType(
                                        TypeInfo<typename AccessTypes::RawType>::ID(), 0))...);

                            const EntityID *entityIDs    = chunk->GetEntityIDs();
                            const bool      hasDestroyed = chunk->HasDestroyedEntities();

                            for (size_t entIdx = 0; entIdx < count; ++entIdx)
                            {
                                if (hasDestroyed && chunk->IsEntityDestroyed(entIdx)) continue;

                                const EntityID id = entityIDs[entIdx];

                                this->ForEachWithIDImpl(func,
                                                        id,
                                                        componentArrays,
                                                        entIdx,
                                                        std::make_index_sequence<sizeof...(AccessTypes)>{});
                            }
                        }
                    },
                    &counter);
            }

            {
                ZoneScopedN("MainThread Wait");

                m_jobSystem->WaitForCounter(&counter);
            }
        }

        // ============================================================
        // 依存解析用の自動申告
        // ============================================================

        // Read<T> を指定された型 ID のリストを返す（TaskScheduler が参照）
        std::vector<TypeID> GetReadTypes() const override
        {
            std::vector<TypeID> res;
            // C++17 の fold 式（カンマ演算子の畳み込み）で AccessTypes... を順に展開する
            //   (..., 式) は AccessTypes の数だけ「式」を並べて順に評価する
            //   IsWrite が false のものだけ res に加える（true なら void() で何もしない）
            (...,
             (AccessTypes::IsWrite
                  ? void()
                  : res.push_back(TypeInfo<typename AccessTypes::RawType>::ID())));
            return res;
        }

        // Write<T> を指定された型 ID のリストを返す。展開の仕組みは GetReadTypes と同じ
        std::vector<TypeID> GetWriteTypes() const override
        {
            std::vector<TypeID> res;
            (...,
             (AccessTypes::IsWrite
                  ? res.push_back(TypeInfo<typename AccessTypes::RawType>::ID())
                  : void()));
            return res;
        }

    private:
        // ============================================================
        // 内部ヘルパー: tuple → 引数展開
        // ============================================================

        // arrays の各配列から i 番目を取り出し、AccessTypes::ParamType にキャストして func に渡す
        // Is... は make_index_sequence が作る 0, 1, 2, ... で、展開すると
        //   func(static_cast<P0>(arr0[i]), static_cast<P1>(arr1[i]), ...)
        // の1回の呼び出しになる。Read<T> は const T& になるので、書き込みはコンパイルで弾かれる
        template <typename Func, typename Tuple, size_t... Is>
        void ForEachImpl(Func &&func, Tuple &arrays, size_t i, std::index_sequence<Is...>)
        {
            func(
                static_cast<
                    typename std::tuple_element_t<Is, std::tuple<AccessTypes...>>::ParamType>(
                    std::get<Is>(arrays)[i])...);
        }

        // ForEachImpl の EntityID 付き版。先頭引数に id を渡す
        template <typename Func, typename Tuple, size_t... Is>
        void ForEachWithIDImpl(Func &&func, EntityID id, Tuple &arrays, size_t i,
                               std::index_sequence<Is...>)
        {
            func(id,
                 static_cast<
                     typename std::tuple_element_t<Is, std::tuple<AccessTypes...>>::ParamType>(
                     std::get<Is>(arrays)[i])...);
        }
    };

} // namespace CCL::ECS
