/**
 * @file   CCL_EventBus.h
 * @brief  ECS内外をつなぐ汎用イベントバス(型消去 + ID基盤)
 *
 * @details
 *  ECSのSystem間通信や、ECS外のUI/サウンド/実績システムへの通知を仲介します。
 *  典型的な使い方は「Systemが Publish<EventType>(data) で発火、
 *  別Systemや外部Managerが Subscribe<EventType>(callback) で受け取る」です。
 *
 *  購読解除のために EventSubscriptionID(チケット番号)を返します。
 *  System破棄時に自動Unsubscribeしたい場合は IfSystem::ListenEvent を推奨。
 *
 *  【設計上の特徴】
 *   - typeid 不使用: 軽量な「型ごとに+1されるID」で型を区別
 *   - 型消去: 各 EventType ごとに専用Wrapper(IEventWrapper派生)を内部生成
 *
 *  【依存】
 *   - 標準C++のみ
 *
 * @author CCL ECS Library
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <algorithm>


namespace CCL::ECS::Core
{
    // =========================================================================
    //  EventTypeIDGenerator
    // =========================================================================

    /**
     * @class EventTypeIDGenerator
     * @brief 各イベント型に対し、軽量な連番IDを発行する
     */
    class EventTypeIDGenerator
    {
    public:
        template <typename T>
        static std::uint32_t GetID() noexcept
        {
            static const std::uint32_t id = NextID();
            return id;
        }

    private:
        static std::uint32_t NextID() noexcept
        {
            static std::atomic<std::uint32_t> s_counter{0};
            return s_counter.fetch_add(1, std::memory_order_relaxed);
        }
    };


    /// @brief Subscribe時に返される購読解除用チケットID
    using EventSubscriptionID = std::uint64_t;


    // =========================================================================
    //  IEventWrapper / EventWrapper<T>
    // =========================================================================

    /// @brief 型消去用の基底クラス
    class IEventWrapper
    {
    public:
        virtual ~IEventWrapper() = default;
    };

    /**
     * @class EventWrapper
     * @brief 型Tのイベントに対する購読者リストを保持する
     */
    template <typename EventType>
    class EventWrapper : public IEventWrapper
    {
    public:
        using CallbackType = std::function<void(const EventType&)>;

        struct Subscriber
        {
            EventSubscriptionID id;
            CallbackType        callback;
        };

        /// @brief コールバックを追加(チケットIDを返す)
        EventSubscriptionID Add(CallbackType cb)
        {
            const EventSubscriptionID id = _nextId++;
            _subscribers.push_back({id, std::move(cb)});
            return id;
        }

        /// @brief 指定IDの購読を解除
        void Remove(EventSubscriptionID id)
        {
            auto it = std::remove_if(_subscribers.begin(), _subscribers.end(),
                [id](const Subscriber& s){ return s.id == id; });
            _subscribers.erase(it, _subscribers.end());
        }

        /// @brief 全購読者にイベントを通知
        void Trigger(const EventType& data) const
        {
            for (const auto& s : _subscribers) {
                if (s.callback) s.callback(data);
            }
        }

    private:
        std::vector<Subscriber> _subscribers;
        EventSubscriptionID     _nextId = 1;
    };


    // =========================================================================
    //  EventBus
    // =========================================================================

    /**
     * @class EventBus
     * @brief イベント型ごとに購読/発行を行う中央バス
     */
    class EventBus
    {
    public:
        EventBus()  = default;
        ~EventBus() = default;

        EventBus(const EventBus&)            = delete;
        EventBus& operator=(const EventBus&) = delete;

        /**
         * @brief イベント型EventTypeを購読する
         * @tparam EventType 受信したいイベント型
         * @param  callback  発行時に呼ばれる関数
         * @return 購読解除用チケットID
         */
        template <typename EventType>
        EventSubscriptionID Subscribe(std::function<void(const EventType&)> callback)
        {
            const std::uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();

            auto it = _subscribers.find(typeId);
            if (it == _subscribers.end()) {
                _subscribers[typeId] = std::make_unique<EventWrapper<EventType>>();
            }

            auto* wrapper = static_cast<EventWrapper<EventType>*>(_subscribers[typeId].get());
            return wrapper->Add(std::move(callback));
        }

        /**
         * @brief 購読を解除する
         * @tparam EventType 購読時の型
         * @param  subId Subscribeが返したチケットID
         */
        template <typename EventType>
        void Unsubscribe(EventSubscriptionID subId)
        {
            const std::uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();
            auto it = _subscribers.find(typeId);
            if (it == _subscribers.end()) return;

            auto* wrapper = static_cast<EventWrapper<EventType>*>(it->second.get());
            wrapper->Remove(subId);
        }

        /**
         * @brief イベントを全購読者に通知
         * @tparam EventType 発行する型
         * @param  data 通知するデータ
         */
        template <typename EventType>
        void Publish(const EventType& data) const
        {
            const std::uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();
            auto it = _subscribers.find(typeId);
            if (it == _subscribers.end()) return;

            const auto* wrapper = static_cast<const EventWrapper<EventType>*>(it->second.get());
            wrapper->Trigger(data);
        }

        /// @brief 全購読を解除
        void Clear() { _subscribers.clear(); }

    private:
        std::unordered_map<std::uint32_t, std::unique_ptr<IEventWrapper>> _subscribers;
    };

} // namespace CCL::ECS::Core
