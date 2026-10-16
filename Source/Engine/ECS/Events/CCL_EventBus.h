#pragma once
// ===========================================================================
// 概要: System 間・ECS 外とのイベント通信を仲介する EventBus。
//
// 責務:
//   - イベント型ごとの購読者リストの管理（購読・解除は IfSystem::ListenEvent 経由のみ）
//   - Publish による購読者への同期通知
//   - Publish ごとに、イベント型名付きの Tracy ゾーンを自動で張る
//
// 備考: 主に「ECS の世界」と「それ以外（UI 更新・サウンド再生・実績解除など）」
//       をつなぐ橋渡しに使う。発行側は購読者を知らなくてよいので依存が減る。
// ===========================================================================
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include <tracy/Tracy.hpp>

namespace CCL::ECS
{

    // Subscribe/Unsubscribe の friend 指定に使う前方宣言
    template <class Derived, class... AccessTypes>
    class IfSystem;

    // ============================================================
    // イベント型 ID
    // ============================================================

    // RTTI の typeid を使わず、イベント型ごとに連番を割り当てる
    class EventTypeIDGenerator
    {
        static inline uint32_t counter = 0;

    public:
        template <typename T>
        static uint32_t GetID()
        {
            // 型 T ごとに、この static 変数が「たった一つだけ」作られる
            static uint32_t id = counter++;
            return id;
        }
    };

    // ============================================================
    // Publish の Tracy ゾーン（イベント型ごとに自動で名前が付く）
    // ============================================================
    //
    // EventBus::Publish は同期呼び出しで、購読者のコールバックを発行元の
    // コールスタック上でそのまま実行する。つまり購読者がやった重い仕事は
    // Tracy 上では「発行した System」の中身として計上される
    //  （例: PlayerJumpFacingSystem の中に SceneSerializer::Deserialize が現れる）。
    // どのイベントが何を連れてきたのかを追えるように、Publish 自体へゾーンを置く。
    //
    // イベント種別ごとにマクロを書かなくて済むように、
    //   ・テンプレートなので EventType ごとに別々の static が実体化される
    //   ・その static に SourceLocationData を1個だけ持たせ、名前を型名にする
    // という形にしている。新しいイベント型を足しても、こちら側の作業はゼロ。
    //
    // Tracy は SourceLocationData をポインタで同一視するので、名前の文字列が
    // ネットワークへ流れるのは型ごとに1回だけ。毎回の発行コストはゾーン1個ぶん。
#ifdef TRACY_ENABLE
    // typeid(T).name() は静的な寿命を持つ文字列。MSVC は "struct Foo" を返すので
    // 接頭辞だけ飛ばして読みやすくする（ポインタを進めるだけなのでコピーは不要）。
    template <typename T>
    inline const char *EventZoneName()
    {
        static const char *const name = [] {
            const char *n = typeid(T).name();
            if (std::strncmp(n, "struct ", 7) == 0) return n + 7;
            if (std::strncmp(n, "class ", 6) == 0) return n + 6;
            return n;
        }();
        return name;
    }

    template <typename T>
    inline const tracy::SourceLocationData *EventPublishSrcLoc()
    {
        static const tracy::SourceLocationData loc{
            EventZoneName<T>(), // ゾーン名 = イベント型名
            "EventBus::Publish",
            __FILE__,
            static_cast<uint32_t>(__LINE__),
            0x8844CC // 紫系。イベント経由の処理だと一目で分かるように
        };
        return &loc;
    }

// Publish 1回ぶんのスコープ。TRACY_ENABLE が無ければ丸ごと消える。
#define CCL_EVENTBUS_PUBLISH_ZONE(EventT) tracy::ScopedZone _ccl_publish_zone(CCL::ECS::EventPublishSrcLoc<EventT>())
#else
#define CCL_EVENTBUS_PUBLISH_ZONE(EventT) (void)0
#endif

    // 購読解除用のチケット
    using EventSubscriptionID = uint64_t;

    // ============================================================
    // イベント型ごとの購読者リスト（型消去）
    // ============================================================

    // 型の違う EventWrapper<T> を1つのマップに並べるための共通基底
    class IEventWrapper
    {
    public:
        virtual ~IEventWrapper() = default;
    };

    template <typename EventType>
    class EventWrapper : public IEventWrapper
    {
    public:
        using CallbackType = std::function<void(const EventType &)>;

        // 解除時にどのコールバックかを特定できるよう、チケット ID と一緒に保存する
        struct Subscriber
        {
            EventSubscriptionID id;
            CallbackType        callback;
        };

        std::vector<Subscriber> subscribers;
        EventSubscriptionID     nextId = 1;

        // コールバックを登録し、解除用のチケットを返す
        EventSubscriptionID Add(CallbackType cb)
        {
            EventSubscriptionID id = nextId++;
            subscribers.push_back({id, cb});
            return id;
        }

        // チケットに対応するコールバックだけを削除する
        void Remove(EventSubscriptionID id)
        {
            auto it = std::remove_if(subscribers.begin(),
                                     subscribers.end(),
                                     [id](const Subscriber &s) { return s.id == id; });
            if (it != subscribers.end())
            {
                subscribers.erase(it, subscribers.end());
            }
        }

        // 全購読者のコールバックを登録順に呼ぶ
        void Trigger(const EventType &eventData)
        {
            for (auto &s : subscribers)
            {
                s.callback(eventData);
            }
        }
    };

    // ============================================================
    // EventBus
    // ============================================================

    class EventBus
    {
    private:
        // イベント型 ID → そのイベント型の購読者リスト
        std::unordered_map<uint32_t, std::unique_ptr<IEventWrapper>> m_subscribers;

        // 購読・解除は private にして、IfSystem::ListenEvent だけが呼べるようにする。
        //
        // なぜ塞ぐか:
        //   ここを直接呼ぶと解除チケットが誰にも管理されず、System が破棄された後も
        //   コールバックがリストに残る。ListenEvent は解除処理を m_eventCleanups に
        //   積んでデストラクタで必ず外すため、購読の入口を1本に強制する。
        //
        // 購読したい System は IfSystem を継承して ListenEvent<EventType>(...) を使うこと。
        template <class Derived, class... AccessTypes>
        friend class IfSystem;

        // 購読する。初めてのイベント型なら購読者リストを作る
        template <typename EventType>
        EventSubscriptionID Subscribe(std::function<void(const EventType &)> callback)
        {
            uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();

            if (m_subscribers.find(typeId) == m_subscribers.end())
            {
                m_subscribers[typeId] = std::make_unique<EventWrapper<EventType>>();
            }

            auto wrapper = static_cast<EventWrapper<EventType> *>(m_subscribers[typeId].get());
            return wrapper->Add(callback);
        }

        // 購読を解除する
        template <typename EventType>
        void Unsubscribe(EventSubscriptionID subId)
        {
            uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();

            if (m_subscribers.find(typeId) != m_subscribers.end())
            {
                auto wrapper = static_cast<EventWrapper<EventType> *>(m_subscribers[typeId].get());
                wrapper->Remove(subId);
            }
        }

    public:
        // イベントを発行し、全購読者へその場で（同期で）通知する
        template <typename EventType>
        void Publish(const EventType &eventData)
        {
            // ここは同期呼び出し。購読者のコストは呼び出し元のゾーンに乗る。
            //   イベント型ごとに別名のゾーンが自動で出るので、Tracy 上で
            //   「このスパイクはどのイベントが連れてきたのか」を辿れる。
            CCL_EVENTBUS_PUBLISH_ZONE(EventType);

            uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();

            if (m_subscribers.find(typeId) != m_subscribers.end())
            {
                auto wrapper = static_cast<EventWrapper<EventType> *>(m_subscribers[typeId].get());
                wrapper->Trigger(eventData);
            }
        }

        // World のリセット時に全購読を破棄する
        void Clear() { m_subscribers.clear(); }
    };

} // namespace CCL::ECS