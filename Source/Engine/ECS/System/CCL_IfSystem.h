#pragma once
// ===========================================================================
// CCL_IfSystem.h
// --------------
// 「必要 Component 構成（Filter）を自動生成し、型安全な ForEach API を
// 提供するテンプレート基底クラス」 IfSystem を定義します。
//
// SystemBase 単体では「処理対象 Chunk を受け取る器」しかありませんが、
// IfSystem を継承すると:
//   - Filter (Archetype) が自動構築
//   - Read<T> / Write<T> による const 強制
//   - ForEach / ForEachParallel / ForEachWithID / ForEachWithIDParallel
//   - イベント購読の自動解除 (ListenEvent)
//   - Read/Write 型の自動申告（TaskScheduler の依存解析向け）
//
// が全てまとめて手に入ります。実装の中心は本ヘッダ。
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

// SystemBase は前方分割済みヘッダから取り込む
#include "CCL_SystemBase.h"
#include "CCL_SystemAccess.h"

// IfSystem の実装内では Chunk / World / JobSystem の完全定義が必要
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
     * @tparam AccessTypes アクセス指定子 (Read<T> / Write<T>) を可変長で指定
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
         * @brief コンストラクタで AccessTypes... から Filter (Archetype) を自動構築。
         *
         * @code
         *   class MovementSystem : public IfSystem<MovementSystem, Write<Position>, Read<Velocity>>
         *   {
         *       MovementSystem() : IfSystem("MovementSystem") {}
         *       ...
         *   };
         *   // この時点で m_filter は {Position, Velocity} に自動設定済み
         * @endcode
         */
        IfSystem(const std::string& name) : SystemBase(name)
        {
            // Read<T>/Write<T> のラッパーから生の型 RawType を取り出して Filter を生成
            m_filter = ArchetypeHelper::Generate<typename AccessTypes::RawType...>();
        }

        /// @brief デストラクタ。登録済みイベント購読を全て自動解除する。
        virtual ~IfSystem() override
        {
            for (auto& cleanup : m_eventCleanups) {
                cleanup();
            }
            m_eventCleanups.clear();
        }


    private:
        /**
         * @brief 自分が登録したイベントの「購読解除コマンド」を保持する箱。
         *
         * std::function 化することで「何のイベント型か」という型情報を内部に
         * 焼き付けつつ表面上は型消去できる（C++ の強力なパターン）。
         */
        std::vector<std::function<void()>> m_eventCleanups;


    protected:
        // ============================================================
        // イベント購読 API
        // ============================================================

        /**
         * @brief 派生クラスから呼ぶ「超簡単イベント登録 API」。
         *
         * 通常 EventBus の Subscribe / Unsubscribe は別々に管理する必要が
         * ありますが、本 API を使うと購読解除関数が自動で m_eventCleanups に
         * 積まれるため、デストラクタで安全に一括解除されます。
         *
         * @tparam EventType 受信したいイベント型
         * @param callback   受信時に呼ばれるコールバック
         */
        template <typename EventType>
        void ListenEvent(std::function<void(const EventType&)> callback)
        {
            if (!m_world) return;

            // 1. EventBus に登録し、チケットIDを受け取る
            auto ticketID = m_world->GetEventBus().Subscribe<EventType>(callback);

            // 2. 「このイベントを解除する」処理そのものをラムダにカプセル化して保管
            //    （EventType が何だったかをラムダ内に焼き付けて保存）
            m_eventCleanups.push_back([this, ticketID]() {
                if (this->m_world) {
                    this->m_world->GetEventBus().Unsubscribe<EventType>(ticketID);
                }
            });
        }

        /**
         * @brief 除外コンポーネントを設定する（コンストラクタ内で呼ぶ想定）。
         *
         * ここで指定した型を「1つでも持つ」Entity は、このSystemの処理対象から
         * 外れます。
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
         * コンポーネントは `T&` として func に渡されます（型レベルで強制）。
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
        void ForEach(Func&& func)
        {
            using namespace CCL::ECS;

            for (Chunk* chunk : m_targetChunks)
            {
                const size_t count = chunk->GetEntityCount();
                if (count == 0) continue;

                // 1. 各コンポーネント配列の生ポインタを一括取得（ループ外で1回だけ）
                auto componentArrays = std::make_tuple(
                    static_cast<typename AccessTypes::RawType*>(
                        chunk->GetComponentPtrByType(
                            TypeInfo<typename AccessTypes::RawType>::ID(), 0))...
                );

                const bool hasDestroyed = chunk->HasDestroyedEntities();

                // 2. Chunk 内の Entity をループ
                for (size_t i = 0; i < count; ++i)
                {
                    // 削除予約済み Entity をスキップ
                    // hasDestroyed が false（大半）なら右辺の重い関数呼び出しを
                    // 短絡評価で省略 → 通常時のパフォーマンスを維持
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
         * @brief ForEach の並列実行版。Chunk をスレッド数で均等割りして処理。
         *
         * 並列化の準備コストが見合わない場合（Chunk 数が少ない、JobSystem
         * が無い、enableMultiThread が false）は自動的にシングルスレッド版へ
         * フォールバックします。
         *
         * @note チャンク単位で並列化するので、同一 Chunk 内の Entity 間で
         *       データ競合は発生しません（Chunk が SoA で独立しているため）。
         */
        template <typename Func>
        void ForEachParallel(Func&& func)
        {
            using namespace CCL::ECS;

            // 並列化が不可能/不要な状況なら直列版にフォールバック
            if (!m_jobSystem || !this->enableMultiThread) {
                ForEach(func);
                return;
            }

            const size_t totalChunks = m_targetChunks.size();
            if (totalChunks == 0) return;

            unsigned int threadCount = std::thread::hardware_concurrency();
            if (threadCount == 0) threadCount = 4;

            // Chunk 数がスレッド数の 2 倍以下なら準備コスト負けするので直列
            if (totalChunks < threadCount * 2) {
                ForEach(func);
                return;
            }

            JobCounter counter;

            // 1ジョブが担当する Chunk 数（均等割り、切り上げ）
            const size_t chunksPerJob = (totalChunks + threadCount - 1) / threadCount;

            // [外側ループ]: スレッド単位で仕事を発行
            for (size_t i = 0; i < totalChunks; i += chunksPerJob)
            {
                const size_t end = (std::min)(i + chunksPerJob, totalChunks);

                m_jobSystem->Execute(
                    [this, i, end, func]() {
                        // 1. まず標準のスコープを作る（関数名がデフォルトになる）
                        ZoneScoped;

                        // 2. SystemBaseの m_name 変数を使って、Tracyに動的文字列をコピーさせる
                        // ※ "MovementSystem [Batch]" のように末尾にバッチ処理であることを明示すると
                        // 通常のUpdateの負荷(メインスレッド)と見分けがつきやすくなります。
                        std::string batchName = " [Batch]" + m_name;
                        ZoneName(batchName.c_str(), batchName.size());


                        // --- ワーカースレッド内 ---
                        // [中ループ]: 担当範囲の Chunk を順に処理
                        for (size_t k = i; k < end; ++k) {
                            Chunk*       chunk = m_targetChunks[k];
                            const size_t count = chunk->GetEntityCount();
                            if (count == 0) continue;

                            // ループ外で全コンポーネント配列を一括取得（高速化の鉄則）
                            auto componentArrays = std::make_tuple(
                                static_cast<typename AccessTypes::RawType*>(
                                    chunk->GetComponentPtrByType(
                                        TypeInfo<typename AccessTypes::RawType>::ID(), 0))...);

                            const bool hasDestroyed = chunk->HasDestroyedEntities();

                            // [内ループ]: Chunk 内の Entity を全走査
                            for (size_t entIdx = 0; entIdx < count; ++entIdx) {
                                // 短絡評価による劇的最適化
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

            // 全ジョブ完了待ち（メインスレッドも暇なら手伝う設計）
            {
 
                ZoneScopedN("MainThread Wait");

                m_jobSystem->WaitForCounter(&counter);
            }
        }


        // ============================================================
        // ForEachWithID（EntityID 付き、シングルスレッド）
        // ============================================================

        /**
         * @brief ForEach の EntityID 付き版。相互作用処理や他 Entity 参照に使用。
         *
         * @code
         *   ForEachWithID([this](EntityID id, Health& h){
         *       if (h.hp <= 0) world->Destroy(id);
         *   });
         * @endcode
         */
        template <typename Func>
        void ForEachWithID(Func&& func)
        {
            using namespace CCL::ECS;

            for (Chunk* chunk : m_targetChunks)
            {
                const size_t count = chunk->GetEntityCount();
                if (count == 0) continue;

                auto componentArrays = std::make_tuple(
                    static_cast<typename AccessTypes::RawType*>(
                        chunk->GetComponentPtrByType(
                            TypeInfo<typename AccessTypes::RawType>::ID(), 0))...
                );

                const EntityID* entityIDs    = chunk->GetEntityIDs();
                const bool      hasDestroyed = chunk->HasDestroyedEntities();

                for (size_t i = 0; i < count; ++i)
                {
                    if (hasDestroyed && chunk->IsEntityDestroyed(i)) continue;

                    // メソッド呼び出しを避けて生ポインタから直接 ID を取得（高速）
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

        /**
         * @brief ForEachWithID の並列実行版。実装は ForEachParallel と同形。
         */
        template <typename Func>
        void ForEachWithIDParallel(Func&& func)
        {
            using namespace CCL::ECS;

            if (!m_jobSystem || !this->enableMultiThread) {
                ForEachWithID(func);
                return;
            }

            const size_t totalChunks = m_targetChunks.size();
            if (totalChunks == 0) return;

            unsigned int threadCount = std::thread::hardware_concurrency();
            if (threadCount == 0) threadCount = 4;

            if (totalChunks < threadCount * 2) {
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
                        // 1. まず標準のスコープを作る（関数名がデフォルトになる）
                        ZoneScoped;

                        // 2. SystemBaseの m_name 変数を使って、Tracyに動的文字列をコピーさせる
                        // ※ "MovementSystem [Batch]" のように末尾にバッチ処理であることを明示すると
                        // 通常のUpdateの負荷(メインスレッド)と見分けがつきやすくなります。
                        std::string batchName = " [ Batch ] " + m_name;
                        ZoneName(batchName.c_str(), batchName.size());


                        for (size_t k = i; k < end; ++k) {
                            Chunk*       chunk = m_targetChunks[k];
                            const size_t count = chunk->GetEntityCount();
                            if (count == 0) continue;

                            // 鉄則: ループ前に全ポインタと状態を抽出
                            auto componentArrays = std::make_tuple(
                                static_cast<typename AccessTypes::RawType*>(
                                    chunk->GetComponentPtrByType(
                                        TypeInfo<typename AccessTypes::RawType>::ID(), 0))...);

                            const EntityID* entityIDs    = chunk->GetEntityIDs();
                            const bool      hasDestroyed = chunk->HasDestroyedEntities();

                            for (size_t entIdx = 0; entIdx < count; ++entIdx) {
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

        /**
         * @brief Read<T> を指定された型 ID のリストを返す（TaskScheduler が参照）。
         */
        std::vector<TypeID> GetReadTypes() const override
        {
            std::vector<TypeID> res;
            // C++17 fold expression で AccessTypes... を順に展開
            // IsWrite が false のものだけリストに加える
            (...,
                (AccessTypes::IsWrite
                     ? void()
                     : res.push_back(TypeInfo<typename AccessTypes::RawType>::ID())));
            return res;
        }

        /**
         * @brief Write<T> を指定された型 ID のリストを返す（TaskScheduler が参照）。
         */
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
         * @brief tuple<T1*, T2*, ...> から i 番目要素を取り出し、
         *        AccessTypes::ParamType にキャストして func に渡す。
         *
         * これにより Read<T> 指定のものは強制的に const T& になり、書き込み
         * 不可となります（コンパイル時に型レベルで保証）。
         */
        template <typename Func, typename Tuple, size_t... Is>
        void ForEachImpl(Func&& func, Tuple& arrays, size_t i, std::index_sequence<Is...>)
        {
            func(
                static_cast<
                    typename std::tuple_element_t<Is, std::tuple<AccessTypes...>>::ParamType>(
                    std::get<Is>(arrays)[i])...);
        }

        /// @brief ForEachImpl の EntityID 付き版
        template <typename Func, typename Tuple, size_t... Is>
        void ForEachWithIDImpl(Func&& func, EntityID id, Tuple& arrays, size_t i,
                                std::index_sequence<Is...>)
        {
            func(id,
                static_cast<
                    typename std::tuple_element_t<Is, std::tuple<AccessTypes...>>::ParamType>(
                    std::get<Is>(arrays)[i])...);
        }
    };

} // namespace CCL::ECS
