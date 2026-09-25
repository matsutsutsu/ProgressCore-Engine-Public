#pragma once
// ===========================================================================
// 概要: 1フレームだけ使う一時データ用のリニアアロケータ FrameAllocator。
//
// 責務:
//   - ページ（1MB の塊）の先頭から順に切り出してメモリを渡す（Alloc / New）
//   - フレーム末の Reset で書き込み位置を先頭へ戻し、ページを再利用する
//   - New で作ったオブジェクトのデストラクタを Reset 時にまとめて呼ぶ
//
// 備考: 個別の解放はできない。確保したページは OS へ返さず、次フレームも使い回す。
// ===========================================================================
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "tracy/Tracy.hpp"

namespace CCL::ECS
{

    class FrameAllocator
    {
    public:
        // pageSize の既定値は 1024 * 1024 = 1MB
        FrameAllocator(size_t pageSize = 1024 * 1024) : m_pageSize(pageSize)
        {
            m_pages.reserve(8);
            // size_t に -1 を入れると最大値になり、AllocateNewPage 内の ++ で 0 に戻る。
            // これで最初のページが 0 ページ目になる
            m_pageIndex = -1;
            AllocateNewPage(pageSize);
        }

        ~FrameAllocator()
        {
            // Reset されずに終了した場合も、New で作ったオブジェクトを必ず破棄する
            RunDestructors();

            for (auto *p : m_pages)
            {
                TracyFree(p);

                ::operator delete(p);
            }
        }

        /**
         * @brief size バイトの領域を確保する（アライメント指定可）。
         * @param alignment 2 のべき乗であること。16 を超える値は想定していない
         * @return 確保した領域の先頭。コンストラクタは呼ばれない
         */
        void *Alloc(size_t size, size_t alignment = 16)
        {
            // サイズではなく「現在の書き込み位置（m_offset）」を、
            // alignment の倍数にぴったり揃うように切り上げる。
            // これで CPU が高速・安全に読める「alignment の倍数の番地」から書き始められる。
            //   (x + a - 1)   … a - 1 を足して、次の倍数をまたがせる
            //   & ~(a - 1)    … 下位ビットを落として a の倍数に切り捨てる
            size_t alignedOffset = (m_offset + alignment - 1) & ~(alignment - 1);

            if (alignedOffset + size > m_pageSize)
            {
                AllocateNewPage(size);

                // 新しいページなので書き込み位置は先頭（0）。
                // ::operator new が返すメモリは 16 バイト境界に揃っているので、0 のままで安全
                alignedOffset = 0;
            }

            void *ptr = m_currentPage + alignedOffset;

            m_offset = alignedOffset + size;

            return ptr;
        }

        /**
         * @brief T を確保して構築する。Alloc との違いはコンストラクタまで呼ぶこと。
         *
         * デストラクタが必要な型（std::vector を持つ型など）は、Reset 時に
         * 自動で破棄されるよう登録しておく。
         */
        template <class T, typename... Args>
        T *New(Args &&...args)
        {
            // 1. 場所を確保
            void *mem = Alloc(sizeof(T));

            // 2. placement new（配置 new）で、確保済みの場所に直接構築する。
            //    普通の new T は「土地を買って家を建てる」こと。
            //    placement new は「既に持っている土地（mem）を指定して、そこに家を建てる」こと。
            T *ptr = new (mem) T(std::forward<Args>(args)...);

            // 3. デストラクタ登録
            //    is_trivially_destructible_v<T> は「T の後片付けが不要か」を判定する。
            //    int や float なら単純な数字なので後片付け不要（何もしない）。
            //    std::vector や std::string を持つクラスは、デストラクタを呼ばないと
            //    メモリリークする。そういう型の場合だけ m_destructors に登録しておく。
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                m_destructors.push_back([ptr]() { ptr->~T(); });
            }

            return ptr;
        }

        /**
         * @brief フレームの終わりに呼ぶ。New したオブジェクトを破棄し、書き込み位置を先頭へ戻す。
         *
         * 確保したページは OS へ返さず、書き込み位置を「0ページ目の先頭」に
         * 戻すだけ。次のフレームでは同じメモリ領域を最初から上書きして再利用する。
         */
        void Reset()
        {
            RunDestructors();

            // このフレームで実際に使ったバイト数を、Tracy 上に折れ線グラフで出す
            size_t totalUsed = (m_pageIndex * m_pageSize) + m_offset;
            TracyPlot("FrameAllocator Usage", static_cast<int64_t>(totalUsed));

            m_pageIndex = 0;
            m_offset    = 0;
            if (!m_pages.empty()) m_currentPage = m_pages[0];
        }

    private:
        void RunDestructors()
        {
            // 後から作ったものが先に作ったものを参照している可能性があるので、生成と逆順に破棄する
            for (auto it = m_destructors.rbegin(); it != m_destructors.rend(); ++it)
            {
                (*it)();
            }
            m_destructors.clear();
        }

        // 今のページに収まらないときに、次のページへ進む
        void AllocateNewPage(size_t requiredSize)
        {
            size_t sizeToAlloc = (std::max)(m_pageSize, requiredSize);

            m_pageIndex++;
            if (m_pageIndex < m_pages.size())
            {
                // 前のフレームで作った2ページ目以降があれば再利用する
                m_currentPage = m_pages[m_pageIndex];
            }
            else
            {
                // 本当に足りない時だけ、OS からメモリをもらう
                uint8_t *ptr = static_cast<uint8_t *>(::operator new(sizeToAlloc));
                m_pages.push_back(ptr);
                m_currentPage = ptr;

                TracyAlloc(ptr, sizeToAlloc);
            }
            m_offset = 0;
        }

        std::vector<uint8_t *> m_pages;       // 確保済みページ（所有する。デストラクタで解放）
        uint8_t               *m_currentPage; // 今書き込んでいるページ
        size_t                 m_offset;      // 今のページ内の書き込み位置（byte）
        size_t                 m_pageIndex;   // 今何ページ目を使っているか
        size_t                 m_pageSize;    // 1ページの大きさ（byte）

        std::vector<std::function<void()>> m_destructors; // Reset 時に呼ぶデストラクタ
    };
} // namespace CCL::ECS
