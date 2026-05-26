/**
 * @file   CCL_IfSystem.h
 * @brief  「特定のコンポーネント構成を持つEntity」だけを処理するSystem基底テンプレ
 *
 * @details
 *  IfSystem<Derived, AccessTypes...> は、AccessTypes として Read<T>/Write<T> を
 *  並べると、自動的に以下を行います:
 *   - フィルタArchetypeを構築 (どのChunkに来るかをSystemManagerが選別)
 *   - GetReadTypes / GetWriteTypes をoverride (TaskSchedulerが並列性を解析可能)
 *   - ForEach系ヘルパで `(const T&, T&)` 形式のループを提供
 *
 *  @par 使用例
 *  @code
 *      class MovementSystem
 *        : public IfSystem<MovementSystem, Write<Position>, Read<Velocity>>
 *      {
 *      public:
 *          MovementSystem() : IfSystem("MovementSystem") {}
 *          void Update(float dt) override {
 *              ForEach([dt](Position& p, const Velocity& v) {
 *                  p.x += v.x * dt;
 *              });
 *          }
 *      };
 *  @endcode
 *
 *  【依存】
 *   - CCL_SystemBase.h
 *   - CCL_SystemAccess.h
 *   - CCL_Chunk.h, CCL_World.h, CCL_JobSystem.h (フルinclude)
 *
 * @author CCL ECS Library
 */
#pragma once

#include "CCL_SystemBase.h"
#include "CCL_SystemAccess.h"
#include "../Core/CCL_Chunk.h"
#include "../Core/CCL_World.h"
#include "../Core/CCL_JobSystem.h"

#include <tuple>
#include <type_traits>
#include <functional>


namespace CCL::ECS
{
    // =========================================================================
    //  IfSystem
    // =========================================================================

    /**
     * @class IfSystem
     * @brief 「Read/Write タグの並びで対象を宣言する」型推論型System基底
     *
     * @tparam Derived     CRTPによる派生クラス自身の型
     * @tparam AccessTypes Read<T> / Write<T> の可変長リスト
     */
    template <class Derived, class... AccessTypes>
    class IfSystem : public SystemBase
    {
    public:
        // =====================================================================
        //  型エイリアス
        // =====================================================================

        /// @brief AccessTypeのリストをtupleとして保持
        using AccessTuple = std::tuple<AccessTypes...>;


        // =====================================================================
        //  コンストラクタ: フィルタを自動構築
        // =====================================================================

        explicit IfSystem(const std::string& name)
            : SystemBase(name)
        {
            // AccessTypes の各 RawType を Archetype に登録
            (RegisterFilter<AccessTypes>(), ...);
        }


        // =====================================================================
        //  Read/Write 型 リスト の override
        // =====================================================================

        std::vector<TypeID> GetReadTypes() const override
        {
            std::vector<TypeID> result;
            (AppendReadID<AccessTypes>(result), ...);
            return result;
        }

        std::vector<TypeID> GetWriteTypes() const override
        {
            std::vector<TypeID> result;
            (AppendWriteID<AccessTypes>(result), ...);
            return result;
        }


        // =====================================================================
        //  イベント購読ヘルパ (自動解除付き)
        // =====================================================================

        /**
         * @brief Worldのイベントバスを購読し、System破棄時に自動解除する
         * @tparam EventType 受信したいイベント型
         * @param  callback 受信時に呼ばれる関数
         */
        template <typename EventType>
        void ListenEvent(std::function<void(const EventType&)> callback)
        {
            if (!_world) return;

            const Core::EventSubscriptionID subId =
                _world->GetEventBus().Subscribe<EventType>(std::move(callback));

            // 派生System破棄時にUnsubscribeするクロージャを記録
            _autoUnsubscribers.emplace_back(
                [w = _world, subId]() { w->GetEventBus().Unsubscribe<EventType>(subId); });
        }

        ~IfSystem() override
        {
            for (auto& un : _autoUnsubscribers) un();
        }


        // =====================================================================
        //  ForEach (シングルスレッド)
        // =====================================================================

        /**
         * @brief 対象Chunkを走査し、各Entityに対して(const T&, T&, ...)を渡してFuncを実行
         * @tparam Func 受け取る引数列が AccessTypes に一致するcallable
         * @param  func 呼び出し本体
         */
        template <typename Func>
        void ForEach(Func&& func)
        {
            std::shared_lock<std::shared_mutex> lock(*_structureMutex);

            for (Core::Chunk* chunk : _targetChunks) {
                ProcessChunkRange(chunk, 0, chunk->GetEntityCount(), func);
            }
        }


        // =====================================================================
        //  ForEachWithID (EntityIDも受け取る版)
        // =====================================================================

        /**
         * @brief ForEachのEntityID付き版
         * @details func(EntityID, const T&, T&, ...) のシグネチャになります。
         */
        template <typename Func>
        void ForEachWithID(Func&& func)
        {
            std::shared_lock<std::shared_mutex> lock(*_structureMutex);

            for (Core::Chunk* chunk : _targetChunks) {
                ProcessChunkRangeWithID(chunk, 0, chunk->GetEntityCount(), func);
            }
        }


        // =====================================================================
        //  ForEachParallel (Chunk単位で並列化)
        // =====================================================================

        /**
         * @brief Chunkを単位として複数スレッドで並列処理する
         * @tparam Func   ForEachと同じシグネチャ
         * @param  func   並列実行する関数
         */
        template <typename Func>
        void ForEachParallel(Func&& func)
        {
            if (!_jobSystem) {
                ForEach(std::forward<Func>(func));
                return;
            }

            Core::JobCounter counter;

            for (Core::Chunk* chunk : _targetChunks) {
                if (chunk->GetEntityCount() == 0) continue;

                _jobSystem->Execute(
                    [this, chunk, func]() {
                        std::shared_lock<std::shared_mutex> lock(*_structureMutex);
                        ProcessChunkRange(chunk, 0, chunk->GetEntityCount(), func);
                    },
                    &counter);
            }

            _jobSystem->WaitForCounter(&counter);
        }


        // =====================================================================
        //  ForEachWithIDParallel
        // =====================================================================

        template <typename Func>
        void ForEachWithIDParallel(Func&& func)
        {
            if (!_jobSystem) {
                ForEachWithID(std::forward<Func>(func));
                return;
            }

            Core::JobCounter counter;

            for (Core::Chunk* chunk : _targetChunks) {
                if (chunk->GetEntityCount() == 0) continue;

                _jobSystem->Execute(
                    [this, chunk, func]() {
                        std::shared_lock<std::shared_mutex> lock(*_structureMutex);
                        ProcessChunkRangeWithID(chunk, 0, chunk->GetEntityCount(), func);
                    },
                    &counter);
            }

            _jobSystem->WaitForCounter(&counter);
        }


    private:
        // =====================================================================
        //  フィルタ構築ヘルパ
        // =====================================================================

        template <class Access>
        void RegisterFilter()
        {
            using Raw = typename Access::RawType;
            _filter.Add<Raw>();
        }

        template <class Access>
        static void AppendReadID(std::vector<TypeID>& out)
        {
            if constexpr (!Access::IsWrite) {
                out.push_back(TypeInfo<typename Access::RawType>::ID());
            }
        }

        template <class Access>
        static void AppendWriteID(std::vector<TypeID>& out)
        {
            if constexpr (Access::IsWrite) {
                out.push_back(TypeInfo<typename Access::RawType>::ID());
            }
        }


        // =====================================================================
        //  Chunk単位処理 (内部ヘルパ)
        // =====================================================================

        template <typename Func>
        void ProcessChunkRange(Core::Chunk* chunk,
                               std::size_t startIdx, std::size_t endIdx,
                               Func& func)
        {
            // 各AccessTypeのRawType*配列を取り出す
            auto arrays = std::make_tuple(
                chunk->template GetComponentArray<typename AccessTypes::RawType>()...
            );

            for (std::size_t i = startIdx; i < endIdx; ++i) {
                if (chunk->IsEntityDestroyed(i)) continue;
                CallFunc(arrays, i, func, std::index_sequence_for<AccessTypes...>{});
            }
        }

        template <typename Func>
        void ProcessChunkRangeWithID(Core::Chunk* chunk,
                                     std::size_t startIdx, std::size_t endIdx,
                                     Func& func)
        {
            auto arrays = std::make_tuple(
                chunk->template GetComponentArray<typename AccessTypes::RawType>()...
            );

            for (std::size_t i = startIdx; i < endIdx; ++i) {
                if (chunk->IsEntityDestroyed(i)) continue;
                const EntityID id = chunk->GetEntityByIndex(i);
                CallFuncWithID(arrays, id, i, func, std::index_sequence_for<AccessTypes...>{});
            }
        }


        // =====================================================================
        //  関数呼び出し (タプルからパック展開)
        // =====================================================================

        template <typename ArraysT, typename Func, std::size_t... Is>
        static void CallFunc(ArraysT& arrays, std::size_t i, Func& func, std::index_sequence<Is...>)
        {
            func(
                static_cast<typename AccessTypes::ParamType>(std::get<Is>(arrays)[i])...
            );
        }

        template <typename ArraysT, typename Func, std::size_t... Is>
        static void CallFuncWithID(ArraysT& arrays, EntityID id, std::size_t i,
                                   Func& func, std::index_sequence<Is...>)
        {
            func(
                id,
                static_cast<typename AccessTypes::ParamType>(std::get<Is>(arrays)[i])...
            );
        }


    private:
        /// @brief 派生System破棄時に呼ぶ自動Unsubscribeリスト
        std::vector<std::function<void()>> _autoUnsubscribers;
    };

} // namespace CCL::ECS
