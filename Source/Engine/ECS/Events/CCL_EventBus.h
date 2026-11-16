#pragma once
// ===========================================================================
// 概要: System 間・ECS 外とのイベント通信を仲介する EventBus
//
// 責務:
//   - イベント型ごとの購読者リストの管理（購読・解除は IfSystem::ListenEvent 経由のみ）
//   - Publish による購読者への同期通知
//   - Publish ごとに、イベント型名付きの Tracy ゾーンを自動で張る
//
// 備考: 主に「ECS の世界」と「それ以外（UI 更新・サウンド再生・実績解除など）」
//       をつなぐ橋渡しに使う。発行側は購読者を知らなくてよいので依存が減る
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

    // イベント型ごとに連番を割り当てる
    class EventTypeIDGenerator
    {
        // 全部の型を共有して管理する変数
        // inlineはヘッダに書いてもプログラム全体で１個にまとめる
        static inline uint32_t counter = 0;

    public:
        // 型 T の番号を返す。型ごとにstatic関数が展開されて
        // その際にstaicの初期化されたidを取得するようになっている
        // そのため型ごとにcounterの別々のIDが取得できる
        template <typename T>
        static uint32_t GetID()
        {
            // 型ごとにidが１個ずつ割り振られる
            // それぞれ最初の呼び出しで１回だけ決まる
            static uint32_t id = counter++;
            return id;
        }
    };

    // ============================================================
    // Publish の Tracy ゾーン（イベント型ごとに自動で名前が付く）
    // ============================================================
    //
    // Publish は購読者のコールバックを発行元のコールスタックでそのまま呼ぶので、
    // 購読者の重い処理は Tracy では発行した System の中に出る
    // どのイベントが何を連れてきたかを追えるよう、
    // Publish 自体にゾーンを置く。名前はイベントの型ごとの static で1回だけ作る
#ifdef TRACY_ENABLE

    // イベントの型の名前を取得する関数
    template <typename T>
    inline const char *EventZoneName()
    {
        // staticで最初の１回だけ初期化する必要があるけど通常なら式１つだけなので
        // ラムダ式にすることで１回だけの処理をstaticに登録することができる
        static const char *const name = [] {
            // typeid(T).name で型の名前を文字列で返す
            // MSVC（ビルド環境）だと最初にstructなど付くので飛ばす
            const char *n = typeid(T).name();
            if (std::strncmp(n, "struct ", 7) == 0) return n + 7; // 先頭の"struct"を飛ばす
            if (std::strncmp(n, "class ", 6) == 0) return n + 6;  // 先頭の"class"を飛ばす
            return n;
        }();
        return name;
    }

    // Tracyの区間を貼る際のポインタを覚える
    // この区間の情報は消えないように保持していく必要があるので
    // staticにしてイベントの型ごとに１つだけを作成する
    template <typename T>
    inline const tracy::SourceLocationData *EventPublishSrcLoc()
    {
        // 型ごとに名前が変わるので自分で区間の情報を組み立てる
        static const tracy::SourceLocationData loc{
            EventZoneName<T>(),              // 区間の名前 = イベントの型名
            "EventBus::Publish",             // 関数名
            __FILE__,                        // ファイル名
            static_cast<uint32_t>(__LINE__), // 行番号
            0x8844CC                         // 色（紫）
        };
        return &loc;
    }

// Publish 1回ぶんのスコープ。TRACY_ENABLE がなければ丸ごと消える
// tracy::ScopedZoneは変数ができた瞬間に
// 区間が始まって}で変数が消えるときに区間が終わる
// このマクロをPublishの先頭に置くことで計測できる
#define CCL_EVENTBUS_PUBLISH_ZONE(EventT) tracy::ScopedZone _ccl_publish_zone(CCL::ECS::EventPublishSrcLoc<EventT>())
#else
#define CCL_EVENTBUS_PUBLISH_ZONE(EventT) (void)0
#endif

    // 購読解除用のチケット
    using EventSubscriptionID = uint64_t;

    // ============================================================
    // イベント型ごとの購読者リスト（型消去）
    // ============================================================

    // 型の違う EventWrapper<T> を1つのマップに並べるための共通基底クラス
    // EventBus側でのイベントを管理するunorder_mapで同じ型として扱うようにするため
    class IEventWrapper
    {
    public:
        // mapの表から削除する際に親としてdeleteをするという記述
        // virtualがないと親だけしか消せずに派生の子供は正しく破棄されない
        virtual ~IEventWrapper() = default;
    };

    // イベントごとに展開される型専用クラス
    template <typename EventType>
    class EventWrapper : public IEventWrapper
    {
    public:
        // イベントの処理を格納できる型を作成
        using CallbackType = std::function<void(const EventType &)>;

        // 解除時にどのコールバックかを特定できるよう、
        // チケット ID と一緒に保存する
        struct Subscriber
        {
            // idは購読しているSystemがひとつだけ破棄された際に
            // その１件だけをsubscribersから外すためにある
            EventSubscriptionID id;         // チケット番号
            CallbackType        callback;   // 呼ぶ関数
        };

        // EventWrapperはイベントがひとつだけでも
        // それをSystem側で複数登録する場合があるので
        // その際の番号とそれぞれの登録された関数処理を保持する
        std::vector<Subscriber> subscribers;
        
        EventSubscriptionID     nextId = 1;

        // コールバックを登録し、解除用のチケットを返す
        EventSubscriptionID Add(CallbackType cb)
        {
            // 今の番号を渡して次の番号へ進める
            EventSubscriptionID id = nextId++;

            // 一覧の末尾に足す
            subscribers.push_back({id, cb});

            // チケットを返す
            return id;
        }

        // チケットに対応するコールバックだけを削除する
        void Remove(EventSubscriptionID id)
        {
            // remove_ifでいらないidを消して後ろの要素を前詰めする
            auto it = std::remove_if(subscribers.begin(),
                                     subscribers.end(),
                                     [id](const Subscriber &s) { return s.id == id; });
            // 実際にvectorの末尾の空き場所を片付ける
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

    // イベントシステムを管理するクラス
    class EventBus
    {
    private:
        // イベント型 ID から そのイベント型の購読者リストを取得できるマップ
        std::unordered_map<uint32_t, std::unique_ptr<IEventWrapper>> m_subscribers;

        // EventBusの登録・解除はprivateなので本来外から呼べないけど
        // IfSystemだけは触れるようにフレンド関数にする
        template <class Derived, class... AccessTypes>
        friend class IfSystem;

        // 購読する。初めてのイベント型なら購読者リストを作る
        template <typename EventType>
        EventSubscriptionID Subscribe(std::function<void(const EventType &)> callback)
        {
            // イベントの型のId取得
            uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();

            // 表になければ
            if (m_subscribers.find(typeId) == m_subscribers.end())
            {
                // 型のリストを生成する
                m_subscribers[typeId] = std::make_unique<EventWrapper<EventType>>();
            }

            // 基底クラスからイベントの型の派生クラスに戻す
            auto wrapper = static_cast<EventWrapper<EventType> *>(m_subscribers[typeId].get());

            // EventWrapperに登録する
            return wrapper->Add(callback);
        }

        // 購読を解除する
        template <typename EventType>
        void Unsubscribe(EventSubscriptionID subId)
        {
            // 型からID取得
            uint32_t typeId = EventTypeIDGenerator::GetID<EventType>();

            // 表にあるなら
            if (m_subscribers.find(typeId) != m_subscribers.end())
            {
                // 派生クラスに戻して削除処理を行う
                auto wrapper = static_cast<EventWrapper<EventType> *>(m_subscribers[typeId].get());
                wrapper->Remove(subId);
            }
        }

    public:
        // イベントを発行し、全購読者へその場で（同期で）通知する
        template <typename EventType>
        void Publish(const EventType &eventData)
        {
            // イベントの型ごとのTracy追跡を行う　処理負荷計測
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

} 
