#pragma once
// ===========================================================================
// 概要: 世代付きハンドル（Handle<T>）で要素を参照する、型ごとのオブジェクトプール。
//
// 責務:
//   - Handle<T>: 添字と世代の2つの uint32 だけを持つ軽いハンドル（コンポーネントに持たせる）
//   - ResourcePool<T>: スロットに T を直接構築・破棄し、空いたスロットを使い回す
//
// 備考: 破棄するとスロットの世代が1つ進むので、破棄前のハンドルで Get すると nullptr が返る
//       （解放済みの領域を読まない）。
//       スロットは std::deque に並べる。deque は末尾に足しても既存の要素を動かさないので、
//       Get で返したポインタは、その要素を破棄するまで有効なまま残る。
// ===========================================================================
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>
#include <deque>

namespace CCL
{

    template <typename T>
    struct Handle
    {
        uint32_t index      = 0xFFFFFFFF; // スロットの添字。0xFFFFFFFF（uint32 の最大値）は無効
        uint32_t generation = 0;          // 発行したときのスロットの世代

        bool IsValid() const { return index != 0xFFFFFFFF; }

        bool operator==(const Handle &other) const
        {
            return index == other.index && generation == other.generation;
        }
        bool operator!=(const Handle &other) const { return !(*this == other); }

        // std::sort や std::map のキーに使うための順序。添字、世代の順に比べる
        bool operator<(const Handle &other) const
        {
            if (index != other.index)
            {
                return index < other.index;
            }
            return generation < other.generation;
        }
    };

    template <typename T>
    class ResourcePool
    {
    private:
        struct Slot
        {
            // T を後から構築するための生のメモリ領域。
            // alignas(T)             T と同じアラインメントにそろえる
            // unsigned char [sizeof(T)] T 1個分の大きさのバイト列（この時点では T ではない）
            alignas(T) unsigned char data[sizeof(T)];
            uint32_t generation = 0;     // 破棄するたびに1つ進む
            bool     active     = false; // T が構築されているか

            // バイト列を T として読むためのキャスト（active のときだけ使う）
            T *GetPtr() { return reinterpret_cast<T *>(data); }

            // const のプールを走査するとき用（ForEachActive は const 関数なので必要）
            const T *GetPtr() const { return reinterpret_cast<const T *>(data); }
        };

        std::deque<Slot>      m_slots;       // スロットの列（追加しても既存のスロットは動かない）
        std::vector<uint32_t> m_freeIndices; // 破棄されて空いたスロットの添字

    public:
        ~ResourcePool() { Clear(); }

        // T を args で構築し、そのハンドルを返す
        template <typename... Args>
        Handle<T> Create(Args &&...args)
        {
            uint32_t index;
            if (!m_freeIndices.empty())
            {
                // 空いたスロットがあれば使い回す
                index = m_freeIndices.back();
                m_freeIndices.pop_back();
            }
            else
            {
                // 無ければ末尾にスロットを1つ足す
                index = static_cast<uint32_t>(m_slots.size());
                m_slots.emplace_back();
            }

            Slot &slot = m_slots[index];

            // placement new: 確保済みの slot.data の上に、T のコンストラクタを直接呼んで構築する
            // （新しくメモリを確保しない）
            new (slot.data) T(std::forward<Args>(args)...);
            slot.active = true;

            return Handle<T>{index, slot.generation};
        }

        // ハンドルが指す T を返す。無効・範囲外・破棄済み（世代が違う）なら nullptr
        T *Get(Handle<T> handle)
        {
            if (!handle.IsValid() || handle.index >= m_slots.size()) return nullptr;

            Slot &slot = m_slots[handle.index];

            // 世代が違うのは、このスロットが一度破棄されて別の T が入っている場合
            if (slot.active && slot.generation == handle.generation)
            {
                return slot.GetPtr();
            }
            return nullptr;
        }

        // ハンドルが指す T を破棄し、スロットを空きに戻す。無効・破棄済みなら何もしない
        void Destroy(Handle<T> handle)
        {
            if (!handle.IsValid() || handle.index >= m_slots.size()) return;

            Slot &slot = m_slots[handle.index];
            if (slot.active && slot.generation == handle.generation)
            {
                // placement new で作ったので delete ではなく、デストラクタだけを明示して呼ぶ
                // （メモリは slot.data のまま残し、次の Create で使い回す）
                slot.GetPtr()->~T();
                slot.active = false;
                slot.generation++; // 古いハンドルで Get しても nullptr になるように

                m_freeIndices.push_back(handle.index);
            }
        }

        // 全要素を破棄してスロットも空にする（シーン切り替えやシャットダウンで使う）
        void Clear()
        {
            for (auto &slot : m_slots)
            {
                if (slot.active)
                {
                    slot.GetPtr()->~T();
                    slot.active = false;
                }
            }
            m_slots.clear();
            m_freeIndices.clear();
        }

        // 構築済みの全要素を func(const T&) に渡す
        template <typename Func>
        void ForEachActive(Func &&func) const
        {
            for (size_t i = 0; i < m_slots.size(); ++i)
            {
                if (m_slots[i].active)
                {
                    func(*m_slots[i].GetPtr());
                }
            }
        }
    };

} // namespace CCL
