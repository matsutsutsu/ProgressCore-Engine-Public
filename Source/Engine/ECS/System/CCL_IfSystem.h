#pragma once
// ===========================================================================
// 概要: 必要 Component 構成（Filter）を自動生成し、型安全な ForEach API を
//       提供する System 用テンプレート基底クラス IfSystem。
//
// 責務:
//   - AccessTypes（Read<T> / Write<T>）から Filter を自動構築する
//   - Read<T> を const T& で渡し、書き込みを型で禁止する
//   - ForEach / ForEachParallel / ForEachWithID / ForEachWithIDParallel の提供
//   - イベント購読の自動解除（ListenEvent）
//   - Read/Write 型を TaskScheduler の依存解析へ自動申告する
//
// 備考: SystemBase 単体は「処理対象 Chunk を受け取る器」でしかない。
//       重いテンプレート展開はすべてこのヘッダに集約している。
//
// @code
//   class MovementSystem
//     : public IfSystem<MovementSystem, Write<Position>, Read<Velocity>>
//   {
//   public:
//       MovementSystem() : IfSystem("MovementSystem") {}
//       void Update(float dt) override {
//           ForEachParallel([dt](Position& p, const Velocity& v){
//               p.x += v.x * dt;
//               p.y += v.y * dt;
//           });
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
    /**
     * @brief 必要 Component 構成を自動生成し、型安全な ForEach API を提供する
     *        System 派生用テンプレート基底クラス。
     *
     * @tparam Derived     CRTP 用の派生クラス自身
     * @tparam AccessTypes アクセス指定子（Read<T> / Write<T>）を可変長で指定
     *
     * 例: `IfSystem<MovementSystem, Write<Position>, Read<Velocity>>`
     *   → Filter は {Position, Velocity}
     *   → ForEach 内で Position は `&`、Velocity は `const &` で受け取れる
     */
    template <class Derived, class... AccessTypes>
    class IfSystem : public SystemBase
    {
    public:
        /**
         * @brief AccessTypes... から Filter（Archetype）を自動構築する。
         * @param name デバッグ・GUI 表示用の名前
         */
        IfSystem(const std::string &name) : SystemBase(name)
        {
            // Read<T>/Write<T> のラッパーから生の型 RawType を取り出して Filter を生成
            m_filter = ArchetypeHelper::Generate<typename AccessTypes::RawType...>();
        }

        // 登録済みのイベント購読を全て自動解除する
        virtual ~IfSystem() override
        {
            for (auto &cleanup : m_eventCleanups)
            {
                cleanup();
            }
            m_eventCleanups.clear();
        }

    private:
        /**
         * @brief ListenEvent で登録したイベントの「購読解除処理」を溜めておく箱。
         *
         * std::function に包むことで「何のイベント型か」をラムダの中に閉じ込めつつ、
         * 外からは同じ型（void()）として並べられる（型消去）。
         */
        std::vector<std::function<void()>> m_eventCleanups;

    protected:
        // ============================================================
        // イベント購読 API
        // ============================================================

        /**
         * @brief イベントを購読する。購読解除はデストラクタで自動的に行われる。
         *
         * EventBus の Subscribe / Unsubscribe を直接使うと解除し忘れの危険がある。
         * この API は解除処理を m_eventCleanups に積むので、System の破棄時に
         * 一括で安全に解除される。
         *
         * @tparam EventType 受信したいイベント型
         * @param callback   受信時に呼ばれるコールバック
         */
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

        /**
         * @brief 除外コンポーネントを設定する（コンストラクタ内で呼ぶ想定）。
         *
         * ここで指定した型を1つでも持つ Entity は、この System の処理対象から外れる。
         *
         * @tparam ExcludeTypes 除外したいコンポーネント型（可変長）
         *
         * @code
         *   class MovementSystem
         *     : public IfSystem<MovementSystem, Write<Position>, Read<Velocity>>
         *   {
         *   public:
         *       MovementSystem() : IfSystem("MovementSystem") {
         *           // DeadTag か Frozen を持つ Entity は動かさない
         *           SetExclude<DeadTag, Frozen>();
         *       }
         *   };
         * @endcode
         */
        template <typename... ExcludeTypes>
        void SetExclude()
        {
            m_excludeFilter = ArchetypeHelper::Generate<ExcludeTypes...>();
        }

        // ============================================================
        // ForEach（シングルスレッド）
        // ============================================================

        /**
         * @brief 全 TargetChunk を順に走査し、各 Entity に対して func を実行する。
         *
         * Read<T> を指定したコンポーネントは `const T&`、Write<T> を指定した
         * コンポーネントは `T&` として func に渡される（型レベルで強制）。
         *
         * @param func ラムダ式。引数は AccessTypes::ParamType...
         *
         * @code
         *   ForEach([dt](Position& p, const Velocity& v){
         *       p.x += v.x * dt;
         *   });
         * @endcode
         */
        template <typename Func>
        void ForEach(Func &&func)
        {
            for (Chunk *chunk : m_targetChunks)
            {
                const size_t count = chunk->GetEntityCount();
                if (count == 0) continue;

                // 1. 各コンポーネント配列の生ポインタを一括取得（ループ外で1回だけ）
                // AccessTypes...                   テンプレートの引数パック。Write<T>/Read<T> の型リスト
                // typename AccessTypes::RawType... 素の型を取り出す → TransformComponent
                // TypeInfo<...>::ID()              型から数値IDを取得
                // static_cast<RawType*>            void* に型を貼りなおす
                // std::make_tuple(...)             それぞれの型の配列ポインタを tuple にまとめる
                // 結論: auto componentArrays = std::make_tuple(
                //         TransformComponent*, VelocityComponent*, RenderComponent* ...);
                auto componentArrays = std::make_tuple(
                    static_cast<typename AccessTypes::RawType *>(
                        chunk->GetComponentPtrByType(
                            TypeInfo<typename AccessTypes::RawType>::ID(), 0))...);

                const bool hasDestroyed = chunk->HasDestroyedEntities();

                // 2. Chunk 内の Entity をループ
                for (size_t i = 0; i < count; ++i)
                {
                    // 削除予約済み Entity をスキップ。
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

        /**
         * @brief ForEach の並列実行版。Chunk をスレッド数で均等割りして処理する。
         *
         * 並列化の準備コストが見合わない場合（Chunk 数が少ない、JobSystem
         * が無い、enableMultiThread が false）は自動的にシングルスレッド版へ
         * フォールバックする。
         *
         * @note Chunk 単位で分割するので、同じ Chunk を2つのジョブが触ることはなく
         *       データ競合は起きない。
         */
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
                        // Tracy 上で System 名が見えるよう、ゾーン名を動的に付け直す。
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

        /**
         * @brief ForEach の EntityID 付き版。相互作用処理や他 Entity の参照に使う。
         *
         * @code
         *   ForEachWithID([this](EntityID id, Health& h){
         *       if (h.hp <= 0) world->Destroy(id);
         *   });
         * @endcode
         */
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
            // C++17 の fold 式（カンマ演算子の畳み込み）で AccessTypes... を順に展開する。
            //   (..., 式) は AccessTypes の数だけ「式」を並べて順に評価する。
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

        /**
         * @brief tuple<T1*, T2*, ...> の各配列から i 番目の要素を取り出し、
         *        AccessTypes::ParamType にキャストして func に渡す。
         *
         * Read<T> 指定のものは強制的に const T& になり、書き込み不可となる
         * （コンパイル時に型レベルで保証）。
         *
         * Is... は 0, 1, 2, ... の整数列（make_index_sequence が生成）。
         * `...` で Is ごとに展開され、最終的に
         *   func(static_cast<P0>(arr0[i]), static_cast<P1>(arr1[i]), ...)
         * という1回の呼び出しになる。
         */
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
