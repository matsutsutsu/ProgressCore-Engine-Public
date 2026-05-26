/**
 * @file   CCL_FrameAllocator.h
 * @brief  フレーム単位でリセットされる超高速ヒープアロケータ
 *
 * @details
 *  「1フレームだけ生きていればいい」一時データのために、
 *  毎フレーム start から書き込み位置だけを巻き戻すリニアアロケータです。
 *  メモリ確保は事実上「ポインタを進めるだけ」なので極めて高速です。
 *
 *  主な利用先: PendingOp の初期データ格納など。
 *
 *  【設計上の注意】
 *   - 確保したメモリは Reset() まで生きる(個別開放不可)
 *   - Reset() 時に登録済みデストラクタが呼ばれる(New<T>使用時のみ)
 *   - 大きすぎる要求(>pageSize)時は専用ページを確保
 *
 *  【依存】
 *   - 標準C++のみ
 *
 * @author CCL ECS Library
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>
#include <type_traits>
#include <new>


namespace CCL::ECS::Core
{
    // =========================================================================
    //  FrameAllocator
    // =========================================================================

    /**
     * @class FrameAllocator
     * @brief リニア確保のフレーム用アロケータ
     */
    class FrameAllocator
    {
    public:
        /**
         * @brief コンストラクタ
         * @param pageSize 1ページの大きさ(デフォルト1MB)
         */
        explicit FrameAllocator(std::size_t pageSize = 1024 * 1024)
            : _pageSize(pageSize)
        {
            _pages.reserve(8);
            _pageIndex = static_cast<std::size_t>(-1); // AllocateNewPage で 0 になる
            AllocateNewPage(pageSize);
        }

        ~FrameAllocator()
        {
            RunDestructors();
            for (auto* p : _pages) {
                ::operator delete(p);
            }
        }

        // コピー/ムーブ禁止
        FrameAllocator(const FrameAllocator&)            = delete;
        FrameAllocator& operator=(const FrameAllocator&) = delete;


        /**
         * @brief 生メモリを確保
         * @param size      確保したいバイト数
         * @param alignment アライメント(デフォルト16byte)
         * @return 確保されたメモリの先頭ポインタ
         *
         * @details
         *  - 書き込み位置を alignment 境界に切り上げ、size を加算して進めるだけ
         *  - 現在ページに収まらない場合は新ページを確保(または既存ページを再利用)
         */
        void* Alloc(std::size_t size, std::size_t alignment = 16)
        {
            std::size_t alignedOffset = (_offset + alignment - 1) & ~(alignment - 1);

            if (alignedOffset + size > _currentPageSize) {
                AllocateNewPage(size);
                alignedOffset = 0;
            }

            void* ptr = _currentPage + alignedOffset;
            _offset   = alignedOffset + size;
            return ptr;
        }

        /**
         * @brief 型Tをこのアロケータ上に構築(Placement New)
         * @tparam T    対象型
         * @tparam Args コンストラクタ引数
         * @param  args 引数
         * @return Tの構築済みポインタ
         *
         * @details
         *  非trivialなデストラクタを持つ場合のみ、Reset() 時に自動的に
         *  デストラクタを呼ぶよう登録します。
         */
        template <class T, typename... Args>
        T* New(Args&&... args)
        {
            void* mem = Alloc(sizeof(T), alignof(T) > 16 ? alignof(T) : 16);
            T*    ptr = new (mem) T(std::forward<Args>(args)...);

            if constexpr (!std::is_trivially_destructible_v<T>) {
                _destructors.emplace_back([ptr]() { ptr->~T(); });
            }
            return ptr;
        }

        /**
         * @brief 全ての確保メモリを"再利用可能"状態にする
         * @details
         *  実メモリは解放しません。書き込み位置を先頭に戻すだけ。
         *  登録されたデストラクタは順次呼ばれます。
         */
        void Reset()
        {
            RunDestructors();
            _pageIndex = 0;
            _offset    = 0;
            if (!_pages.empty()) {
                _currentPage     = _pages[0];
                _currentPageSize = _pageSizes[0];
            }
        }

    private:
        /// @brief 登録されたデストラクタを逆順に実行(生成順の逆)
        void RunDestructors()
        {
            for (auto it = _destructors.rbegin(); it != _destructors.rend(); ++it) {
                (*it)();
            }
            _destructors.clear();
        }

        /**
         * @brief 必要に応じて新規ページを確保(または既存ページを再利用)
         * @param requiredSize 必要なバイト数(pageSizeを超える場合は専用ページ確保)
         */
        void AllocateNewPage(std::size_t requiredSize)
        {
            const std::size_t sizeToAlloc = (std::max)(_pageSize, requiredSize);
            ++_pageIndex;

            if (_pageIndex < _pages.size()) {
                // 既存ページが要求サイズに足りていれば再利用
                if (_pageSizes[_pageIndex] >= sizeToAlloc) {
                    _currentPage     = _pages[_pageIndex];
                    _currentPageSize = _pageSizes[_pageIndex];
                } else {
                    // 足りなければ作り直し
                    ::operator delete(_pages[_pageIndex]);
                    auto* ptr = static_cast<std::uint8_t*>(::operator new(sizeToAlloc));
                    _pages[_pageIndex]     = ptr;
                    _pageSizes[_pageIndex] = sizeToAlloc;
                    _currentPage     = ptr;
                    _currentPageSize = sizeToAlloc;
                }
            } else {
                auto* ptr = static_cast<std::uint8_t*>(::operator new(sizeToAlloc));
                _pages.push_back(ptr);
                _pageSizes.push_back(sizeToAlloc);
                _currentPage     = ptr;
                _currentPageSize = sizeToAlloc;
            }
            _offset = 0;
        }

    private:
        std::vector<std::uint8_t*> _pages;     ///< 各ページの先頭ポインタ
        std::vector<std::size_t>   _pageSizes; ///< 各ページのバイト数

        std::uint8_t*    _currentPage     = nullptr; ///< 現在書き込み中のページ
        std::size_t      _currentPageSize = 0;        ///< 現在ページのサイズ
        std::size_t      _offset          = 0;        ///< 現在ページ内の書き込み位置
        std::size_t      _pageIndex       = 0;        ///< 何ページ目を使っているか
        std::size_t      _pageSize        = 0;        ///< 標準ページサイズ

        std::vector<std::function<void()>> _destructors; ///< Reset時に呼ぶデストラクタ
    };

} // namespace CCL::ECS::Core
