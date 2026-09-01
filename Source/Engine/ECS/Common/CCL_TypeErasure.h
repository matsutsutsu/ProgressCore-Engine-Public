#pragma once
// ===========================================================================
// CCL_TypeErasure.h
// -----------------
// 「型情報が消えた生メモリ (void*) 」に対して、
// その型本来のコンストラクタ・デストラクタを正しく呼び出すための
// 「型消去（Type Erasure）」ユーティリティ群です。
//
// Chunk は SoA 形式で複数の型のコンポーネントを生メモリで保持しますが、
// その時点では型情報が消えています。エンティティ削除時や
// Chunk → Chunk 移動時に「正しい型」として破棄・コピーするため、
// 事前に「掃除の仕方」「複製の仕方」を関数ポインタとして登録しておきます。
// ===========================================================================

#include <new> // placement new


namespace CCL::ECS
{
    // ------------------------------------------------------------------
    // Destructor
    // ------------------------------------------------------------------

    /**
     * @brief 任意型 T のデストラクタを、型を消した状態で呼び出せるよう包んだ
     *        型消去ユーティリティ。
     *
     * 用途:
     *   - Chunk 内で void* として保持しているコンポーネントの破棄
     *   - PendingOp で型を保持できないコンポーネントの安全な解放
     *
     * 使い方:
     * @code
     *   Destructor d;
     *   d.Create<Velocity>();        // この Destructor は Velocity 担当
     *   d.Destruct(rawPointer);      // void* → Velocity* に戻して ~Velocity()
     * @endcode
     */
    class Destructor
    {
    public:
        Destructor()  = default;
        ~Destructor() = default;

        /**
         * @brief この Destructor が破棄を担当する型 T を登録する。
         *
         * 型 T 用のデストラクタ呼び出し関数 (CallDestructorFromPtr<T>) を
         * 関数ポインタとして保存します。これ以降、型情報を失った void* に
         * 対しても、Destruct() を呼び出すだけで本来のデストラクタが走ります。
         */
        template <class T>
        void Create()
        {
            destructFunc = &CallDestructorFromPtr<T>;
        }

        /**
         * @brief 登録済みの破棄関数を実行する。
         * @param dataPtr 破棄したいオブジェクトへの生ポインタ
         */
        void Destruct(void* dataPtr) const
        {
            if (destructFunc) destructFunc(dataPtr);
        }

        /**
         * @brief void* を T* に復元して T のデストラクタを直接呼ぶ静的ヘルパー。
         *
         * reinterpret_cast によって型を復元し、明示的にデストラクタを呼び出します。
         * Chunk のメモリは placement new で構築されているため、
         * delete ではなく明示的なデストラクタ呼び出しが必要です。
         */
        template <class T>
        static void CallDestructorFromPtr(void* dataPtr)
        {
            // void* → T* に復元してデストラクタを呼ぶ
            if (dataPtr) reinterpret_cast<T*>(dataPtr)->~T();
        }

    private:
        // 関数ポインタの読み方:
        //   void  (*destructFunc)(void*) = nullptr;
        //    └戻値  └ポインタ名         └引数リスト
        // → 「void* を1つ受け取って void を返す関数」を指すポインタ
        void (*destructFunc)(void*) = nullptr;
    };


    // ------------------------------------------------------------------
    // Copier
    // ------------------------------------------------------------------

    /**
     * @brief 型情報が消えた void* に対して、正しいコピーコンストラクタを
     *        呼び出すための型消去ユーティリティ。
     *
     * 主な用途:
     *   - Entity が別の Archetype に移動するときのデータ複製
     *   - Chunk が満杯になり別 Chunk へ詰め替えるときの複製
     *
     * いずれも「型情報は失われているが、データは保持したまま移動したい」
     * というシナリオで必要になります。
     */
    class Copier
    {
    public:
        // この Copier がコピーを担当する型 T を登録する
        template <class T>
        void Create()
        {
            copyFunc = &CallCopyFromPtr<T>;
        }

        /**
         * @brief src の中身を dst に対してコピー構築する。
         * @param dst コピー先の未初期化メモリ
         * @param src コピー元のオブジェクト
         */
        void Copy(void* dst, const void* src) const
        {
            if (copyFunc) copyFunc(dst, src);
        }

    private:
        /**
         * @brief src を T* として読み、dst に placement new で T を構築する。
         *
         *   new (dst) T(*reinterpret_cast<const T*>(src));
         *    │            └─ void* を const T* に復元してコピー構築
         *    └─ placement new: 既に確保済みのメモリ位置に直接構築
         */
        template <class T>
        static void CallCopyFromPtr(void* dst, const void* src)
        {
            new (dst) T(*reinterpret_cast<const T*>(src));
        }

        void (*copyFunc)(void*, const void*) = nullptr;
    };

} // namespace CCL::ECS
