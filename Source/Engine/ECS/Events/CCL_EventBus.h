#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * @brief イベントバス (EventBus) の主な役割は、異なるシステム間でのイベント通信を管理することです。
 * 
 * イベントバスは、特定のイベント型に対して購読者を登録し、イベントが発生した際に全ての購読者に通知します。
 * これにより、システム間の依存関係を減らし、柔軟で拡張可能なアーキテクチャを実現します。
 * 主に「ECSの世界」と「それ以外の世界」をつなぐ橋渡し役として機能します
 * 例：UI更新・サウンド再生・実績の解除など、ECSの外部に影響を与えるイベントを発行するために使う
 */

namespace CCL::ECS {

    // Subscribe/Unsubscribe の friend 指定に使う前方宣言。
    template <class Derived, class... AccessTypes> class IfSystem;

    // =======================================================
    // 1. 型IDジェネレータ（RTTI の typeid を使わず、型ごとに連番を割り当てる）
    // =======================================================
    class EventTypeIDGenerator {
        static inline uint32_t counter = 0;

      public:
        template <typename T> static uint32_t GetID()
        {
            // 型 T ごとに、この static 変数が「たった一つだけ」作られる
            static uint32_t id = counter++;
            return id;
        }
    };

    // 購読解除用のチケット
    using EventSubscriptionID = uint64_t;

    // =======================================================
    // 2. イベントの箱 (型の消失 / 型消去)
    // =======================================================
    class IEventWrapper {
      public:
        virtual ~IEventWrapper() = default;
    };

    template <typename EventType> class EventWrapper : public IEventWrapper {
      public:
        using CallbackType = std::function<void(const EventType &)>;

        // 誰の関数かを特定するために、チケットIDと一緒に保存する
        struct Subscriber {
            EventSubscriptionID id;
            CallbackType        callback;
        };

        std::vector<Subscriber> subscribers;
        EventSubscriptionID     nextId = 1;

        // 関数を登録し、キャンセル用のチケットを返す
        EventSubscriptionID Add(CallbackType cb)
        {
            EventSubscriptionID id = nextId++;
            subscribers.push_back({id, cb});
            return id;
        }

        // チケットを使って特定の関数だけを削除する
        void Remove(EventSubscriptionID id)
        {
            auto it = std::remove_if(subscribers.begin(),
                subscribers.end(),
                [id](const Subscriber &s) { return s.id == id; });
            if (it != subscribers.end()) {
                subscribers.erase(it, subscribers.end());
            }
        }

        // 全員に手紙を配る (イベント発火)
        void Trigger(const EventType &eventData)
        {
            for (auto &s : subscribers) {
                s.callback(eventData);
            }
        }
    };

    // =======================================================
    // 3. 郵便局本体 (EventBus)
    // =======================================================
    class EventBus {
      private:
        // 「型のID」と「そのイベント専用の箱」のペアを管理する名簿
        std::unordered_map<uint32_t, std::unique_ptr<IEventWrapper>> m_subscribers;

        // -------------------------------------------------------
        // 購読・解除は private（IfSystem::ListenEvent だけが呼べる）
        //
        // なぜ塞ぐか:
        //   ここを直接叩くと解除チケットが誰にも管理されず、System が破棄された後も
        //   コールバックが名簿に残る。ListenEvent は解除処理を m_eventCleanups に
        //   積んでデストラクタで必ず外すため、購読の入口を1本に強制する。
        //
        // 購読したい System は IfSystem を継承して ListenEvent<EventType>(...) を使うこと。
        // -------------------------------------------------------
        template <class Derived, class... AccessTypes> friend class IfSystem;

        // 購読する (手紙が欲しいと登録する)
        template <typename EventType>
        EventSubscriptionID Subscribe(std::function<void(const EventType &)> callback)
        {
            uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();

            if (m_subscribers.find(typeId) == m_subscribers.end()) {
                m_subscribers[typeId] = std::make_unique<EventWrapper<EventType>>();
            }

            auto wrapper = static_cast<EventWrapper<EventType> *>(m_subscribers[typeId].get());
            return wrapper->Add(callback);
        }

        // 購読を解除する (もう手紙は要らないと伝える)
        template <typename EventType> void Unsubscribe(EventSubscriptionID subId)
        {
            uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();

            if (m_subscribers.find(typeId) != m_subscribers.end()) {
                auto wrapper = static_cast<EventWrapper<EventType> *>(m_subscribers[typeId].get());
                wrapper->Remove(subId);
            }
        }

      public:
        // 手紙を投函する (全員に配る)
        template <typename EventType> void Publish(const EventType &eventData)
        {
            uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();

            if (m_subscribers.find(typeId) != m_subscribers.end()) {
                auto wrapper = static_cast<EventWrapper<EventType> *>(m_subscribers[typeId].get());
                wrapper->Trigger(eventData);
            }
        }

        // 世界がリセットされる時に名簿を空にする
        void Clear() { m_subscribers.clear(); }
    };

} // namespace CCL::ECS